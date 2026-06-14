#include "submit_handler.hpp"
#include "auth_handler.hpp"
#include "../service/auth_service.hpp"
#include "../service/problem_service.hpp"
#include "../service/executor_service.hpp"
#include "../model/problem.hpp"
#include "../db/connection_pool.hpp"
#include "../utils/config.hpp"
#include "../utils/logger.hpp"

#include <httplib.h>
#include <json.hpp>
#include <mysql/mysql.h>
#include <vector>

using json = nlohmann::json;

static json json_error(const std::string& msg) {
    json j;
    j["error"] = msg;
    return j;
}

static std::string escape(const std::string& str, MYSQL* conn) {
    if (str.empty()) return str;
    std::vector<char> buf(str.length() * 2 + 1);
    mysql_real_escape_string(conn, buf.data(), str.data(), str.length());
    return std::string(buf.data());
}

void handle_submit(const httplib::Request& req, httplib::Response& res) {
    auto user = require_auth(req);
    if (user.user_id <= 0) {
        res.status = 401;
        res.set_content(json_error("Not authenticated").dump(), "application/json");
        return;
    }

    json body;
    try { body = json::parse(req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(json_error("Invalid JSON").dump(), "application/json");
        return;
    }

    int problem_id = body.value("problem_id", 0);
    std::string code = body.value("code", "");

    if (problem_id <= 0 || code.empty()) {
        res.status = 400;
        res.set_content(json_error("problem_id and code required").dump(), "application/json");
        return;
    }

    auto max_size = Config::instance().rate_limit().max_code_size;
    if (code.size() > (size_t)max_size) {
        res.status = 413;
        res.set_content(json_error("Code too large (" + std::to_string(code.size())
                                   + " > " + std::to_string(max_size) + ")").dump(),
                        "application/json");
        return;
    }

    if (!AuthService::instance().check_rate_limit(user.user_id)) {
        res.status = 429;
        res.set_content(json_error("Rate limited. Please wait before submitting again.")
                        .dump(), "application/json");
        return;
    }

    if (!ProblemService::instance().problem_exists(problem_id)) {
        res.status = 404;
        res.set_content(json_error("Problem not found").dump(), "application/json");
        return;
    }

    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) {
        res.status = 500;
        res.set_content(json_error("Internal error").dump(), "application/json");
        return;
    }

    std::string q = "INSERT INTO submissions (user_id, problem_id, code, status) VALUES ("
                    + std::to_string(user.user_id) + ", "
                    + std::to_string(problem_id) + ", '"
                    + escape(code, conn) + "', 'PENDING')";

    if (mysql_query(conn, q.c_str()) != 0) {
        Logger::instance().error("Insert submission failed: " + std::string(mysql_error(conn)));
        pool.release(conn);
        res.status = 500;
        res.set_content(json_error("Failed to create submission").dump(), "application/json");
        return;
    }

    int submission_id = mysql_insert_id(conn);
    pool.release(conn);

    auto p = ProblemService::instance().get_problem_detail(problem_id);

    bool queued = ExecutorService::instance().enqueue(
        submission_id, problem_id, user.user_id, code,
        p.time_limit, p.memory_limit);

    if (!queued) {
        res.status = 503;
        res.set_content(json_error("Judge queue is full").dump(), "application/json");
        return;
    }

    json j;
    j["submission_id"] = submission_id;
    j["status"] = "PENDING";
    res.status = 202;
    res.set_content(j.dump(), "application/json");
}

void handle_get_submission(const httplib::Request& req, httplib::Response& res) {
    auto user = require_auth(req);
    if (user.user_id <= 0) {
        res.status = 401;
        res.set_content(json_error("Not authenticated").dump(), "application/json");
        return;
    }

    int submission_id = std::stoi(req.path_params.at("id"));

    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) {
        res.status = 500;
        res.set_content(json_error("Internal error").dump(), "application/json");
        return;
    }

    std::string q = "SELECT id, problem_id, status, failed_case, error_msg, "
                    "time_used, memory_used, created_at "
                    "FROM submissions WHERE id=" + std::to_string(submission_id)
                    + " AND user_id=" + std::to_string(user.user_id);

    if (mysql_query(conn, q.c_str()) != 0) {
        pool.release(conn);
        res.status = 500;
        res.set_content(json_error("Query failed").dump(), "application/json");
        return;
    }

    auto* result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0) {
        mysql_free_result(result);
        pool.release(conn);
        res.status = 404;
        res.set_content(json_error("Submission not found").dump(), "application/json");
        return;
    }

    auto* row = mysql_fetch_row(result);
    json j;
    j["id"] = std::stoi(row[0]);
    j["problem_id"] = std::stoi(row[1]);
    j["status"] = row[2] ? row[2] : "PENDING";
    int failed_case = row[3] ? std::stoi(row[3]) : 0;
    j["failed_case"] = failed_case;
    j["error_msg"] = row[4] ? row[4] : "";
    j["time_used"] = row[5] ? std::stoi(row[5]) : 0;
    j["memory_used"] = row[6] ? std::stoi(row[6]) : 0;
    j["created_at"] = row[7] ? row[7] : "";
    int problem_id = std::stoi(row[1]);

    mysql_free_result(result);

    if (failed_case > 0) {
        std::string tc_q = "SELECT input, expected FROM testcases "
                           "WHERE problem_id=" + std::to_string(problem_id)
                           + " ORDER BY sort_order, id"
                           + " LIMIT " + std::to_string(failed_case - 1) + ", 1";

        if (mysql_query(conn, tc_q.c_str()) == 0) {
            auto* tc_result = mysql_store_result(conn);
            if (tc_result && mysql_num_rows(tc_result) > 0) {
                auto* tc_row = mysql_fetch_row(tc_result);
                j["failed_input"] = tc_row[0] ? tc_row[0] : "";
                j["failed_expected"] = tc_row[1] ? tc_row[1] : "";
            } else {
                j["failed_input"] = "";
                j["failed_expected"] = "";
            }
            if (tc_result) mysql_free_result(tc_result);
        }
    }

    pool.release(conn);

    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void handle_get_submissions(const httplib::Request& req, httplib::Response& res) {
    auto user = require_auth(req);
    if (user.user_id <= 0) {
        res.status = 401;
        res.set_content(json_error("Not authenticated").dump(), "application/json");
        return;
    }

    int problem_id = 0;
    auto prob_str = req.get_param_value("problem_id");
    if (!prob_str.empty()) {
        problem_id = std::stoi(prob_str);
    }

    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) {
        res.status = 500;
        res.set_content(json_error("Internal error").dump(), "application/json");
        return;
    }

    std::string q = "SELECT id, problem_id, status, failed_case, error_msg, "
                    "time_used, memory_used, created_at "
                    "FROM submissions WHERE user_id=" + std::to_string(user.user_id);

    if (problem_id > 0) {
        q += " AND problem_id=" + std::to_string(problem_id);
    }

    q += " ORDER BY created_at DESC LIMIT 50";

    if (mysql_query(conn, q.c_str()) != 0) {
        pool.release(conn);
        res.status = 500;
        res.set_content(json_error("Query failed").dump(), "application/json");
        return;
    }

    auto* result = mysql_store_result(conn);
    json arr = json::array();

    if (result) {
        while (auto* row = mysql_fetch_row(result)) {
            json j;
            j["id"] = std::stoi(row[0]);
            j["problem_id"] = std::stoi(row[1]);
            j["status"] = row[2] ? row[2] : "PENDING";
            j["failed_case"] = row[3] ? std::stoi(row[3]) : 0;
            j["error_msg"] = row[4] ? row[4] : "";
            j["time_used"] = row[5] ? std::stoi(row[5]) : 0;
            j["memory_used"] = row[6] ? std::stoi(row[6]) : 0;
            j["created_at"] = row[7] ? row[7] : "";
            arr.push_back(std::move(j));
        }
        mysql_free_result(result);
    }

    pool.release(conn);
    res.status = 200;
    res.set_content(arr.dump(), "application/json");
}
