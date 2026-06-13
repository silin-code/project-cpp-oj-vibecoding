#include "problem_service.hpp"
#include "../model/problem.hpp"
#include "../model/test_case.hpp"
#include "../db/connection_pool.hpp"
#include "../utils/config.hpp"
#include "../utils/logger.hpp"

#include <mysql/mysql.h>
#include <sstream>
#include <vector>

ProblemService& ProblemService::instance() {
    static ProblemService inst;
    return inst;
}

bool ProblemService::init() {
    Logger::instance().info("ProblemService initialized");
    return true;
}

static std::string escape(const std::string& str, MYSQL* conn) {
    if (str.empty()) return str;
    std::vector<char> buf(str.length() * 2 + 1);
    mysql_real_escape_string(conn, buf.data(), str.data(), str.length());
    return std::string(buf.data());
}

std::vector<Problem> ProblemService::get_problem_list() {
    std::vector<Problem> problems;
    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) return problems;

    std::string q = R"(
        SELECT p.id, p.title, p.difficulty,
               COUNT(s.id) AS total_sub,
               SUM(CASE WHEN s.status = 'AC' THEN 1 ELSE 0 END) AS ac_count
        FROM problems p
        LEFT JOIN submissions s ON p.id = s.problem_id
        GROUP BY p.id
        ORDER BY p.id
    )";

    if (mysql_query(conn, q.c_str()) != 0) {
        Logger::instance().error("get_problem_list query failed: " + std::string(mysql_error(conn)));
        pool.release(conn);
        return problems;
    }

    auto* result = mysql_store_result(conn);
    if (!result) {
        pool.release(conn);
        return problems;
    }

    while (auto* row = mysql_fetch_row(result)) {
        Problem p;
        p.id = std::stoi(row[0]);
        p.title = row[1] ? row[1] : "";
        p.difficulty = row[2] ? row[2] : "easy";
        p.total_submissions = row[3] ? std::stoi(row[3]) : 0;
        p.accepted = row[4] ? std::stoi(row[4]) : 0;
        p.pass_rate = p.total_submissions > 0
            ? (100.0 * p.accepted / p.total_submissions) : 0.0;
        problems.push_back(std::move(p));
    }

    mysql_free_result(result);
    pool.release(conn);
    return problems;
}

Problem ProblemService::get_problem_detail(int problem_id) {
    Problem p;
    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) return p;

    std::string q = "SELECT id, title, description, input_desc, output_desc, "
                    "difficulty, time_limit, memory_limit, created_at, updated_at "
                    "FROM problems WHERE id = " + std::to_string(problem_id);

    if (mysql_query(conn, q.c_str()) != 0) {
        pool.release(conn);
        return p;
    }

    auto* result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0) {
        mysql_free_result(result);
        pool.release(conn);
        return p;
    }

    auto* row = mysql_fetch_row(result);
    p.id = std::stoi(row[0]);
    p.title = row[1] ? row[1] : "";
    p.description = row[2] ? row[2] : "";
    p.input_desc = row[3] ? row[3] : "";
    p.output_desc = row[4] ? row[4] : "";
    p.difficulty = row[5] ? row[5] : "easy";
    p.time_limit = row[6] ? std::stoi(row[6]) : 2;
    p.memory_limit = row[7] ? std::stoi(row[7]) : 256;
    p.created_at = row[8] ? row[8] : "";
    p.updated_at = row[9] ? row[9] : "";
    mysql_free_result(result);

    p.sample_cases = get_testcases(problem_id, false);

    pool.release(conn);
    return p;
}

bool ProblemService::problem_exists(int problem_id) {
    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) return false;

    std::string q = "SELECT 1 FROM problems WHERE id = " + std::to_string(problem_id);
    bool exists = false;

    if (mysql_query(conn, q.c_str()) == 0) {
        auto* result = mysql_store_result(conn);
        if (result) {
            exists = mysql_num_rows(result) > 0;
            mysql_free_result(result);
        }
    }

    pool.release(conn);
    return exists;
}

int ProblemService::create_problem(
    const std::string& title, const std::string& description,
    const std::string& input_desc, const std::string& output_desc,
    const std::string& difficulty, int time_limit, int memory_limit)
{
    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) return -1;

    std::string q = "INSERT INTO problems (title, description, input_desc, output_desc, "
                    "difficulty, time_limit, memory_limit) VALUES ('"
                    + escape(title, conn) + "', '"
                    + escape(description, conn) + "', '"
                    + escape(input_desc, conn) + "', '"
                    + escape(output_desc, conn) + "', '"
                    + escape(difficulty, conn) + "', "
                    + std::to_string(time_limit) + ", "
                    + std::to_string(memory_limit) + ")";

    if (mysql_query(conn, q.c_str()) != 0) {
        Logger::instance().error("create_problem failed: " + std::string(mysql_error(conn)));
        pool.release(conn);
        return -1;
    }

    int id = mysql_insert_id(conn);
    pool.release(conn);
    Logger::instance().info("Problem created: id=" + std::to_string(id));
    return id;
}

bool ProblemService::update_problem(
    int problem_id, const std::string& title, const std::string& description,
    const std::string& input_desc, const std::string& output_desc,
    const std::string& difficulty, int time_limit, int memory_limit)
{
    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) return false;

    std::string q = "UPDATE problems SET title='" + escape(title, conn)
                    + "', description='" + escape(description, conn)
                    + "', input_desc='" + escape(input_desc, conn)
                    + "', output_desc='" + escape(output_desc, conn)
                    + "', difficulty='" + escape(difficulty, conn)
                    + "', time_limit=" + std::to_string(time_limit)
                    + ", memory_limit=" + std::to_string(memory_limit)
                    + " WHERE id=" + std::to_string(problem_id);

    bool ok = mysql_query(conn, q.c_str()) == 0;
    if (!ok) {
        Logger::instance().error("update_problem failed: " + std::string(mysql_error(conn)));
    }

    pool.release(conn);
    return ok;
}

bool ProblemService::delete_problem(int problem_id) {
    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) return false;

    std::string q = "DELETE FROM problems WHERE id=" + std::to_string(problem_id);
    bool ok = mysql_query(conn, q.c_str()) == 0;

    if (!ok) {
        Logger::instance().error("delete_problem failed: " + std::string(mysql_error(conn)));
    }

    pool.release(conn);
    return ok;
}

std::vector<TestCase> ProblemService::get_testcases(int problem_id, bool include_hidden) {
    std::vector<TestCase> cases;
    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) return cases;

    std::string q = "SELECT id, problem_id, input, expected, is_sample, sort_order "
                    "FROM testcases WHERE problem_id=" + std::to_string(problem_id);

    if (!include_hidden) {
        q += " AND is_sample=1";
    }

    q += " ORDER BY sort_order, id";

    if (mysql_query(conn, q.c_str()) != 0) {
        pool.release(conn);
        return cases;
    }

    auto* result = mysql_store_result(conn);
    if (!result) {
        pool.release(conn);
        return cases;
    }

    while (auto* row = mysql_fetch_row(result)) {
        TestCase tc;
        tc.id = std::stoi(row[0]);
        tc.problem_id = std::stoi(row[1]);
        tc.input = row[2] ? row[2] : "";
        tc.expected = row[3] ? row[3] : "";
        tc.is_sample = row[4] && std::string(row[4]) == "1";
        tc.sort_order = row[5] ? std::stoi(row[5]) : 0;
        cases.push_back(std::move(tc));
    }

    mysql_free_result(result);
    pool.release(conn);
    return cases;
}

int ProblemService::add_testcase(
    int problem_id, const std::string& input, const std::string& expected,
    bool is_sample, int sort_order)
{
    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) return -1;

    std::string q = "INSERT INTO testcases (problem_id, input, expected, is_sample, sort_order) VALUES ("
                    + std::to_string(problem_id) + ", '"
                    + escape(input, conn) + "', '"
                    + escape(expected, conn) + "', "
                    + (is_sample ? "1" : "0") + ", "
                    + std::to_string(sort_order) + ")";

    if (mysql_query(conn, q.c_str()) != 0) {
        Logger::instance().error("add_testcase failed: " + std::string(mysql_error(conn)));
        pool.release(conn);
        return -1;
    }

    int id = mysql_insert_id(conn);
    pool.release(conn);
    return id;
}

bool ProblemService::delete_testcase(int tc_id) {
    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) return false;

    std::string q = "DELETE FROM testcases WHERE id=" + std::to_string(tc_id);
    bool ok = mysql_query(conn, q.c_str()) == 0;

    if (!ok) {
        Logger::instance().error("delete_testcase failed: " + std::string(mysql_error(conn)));
    }

    pool.release(conn);
    return ok;
}
