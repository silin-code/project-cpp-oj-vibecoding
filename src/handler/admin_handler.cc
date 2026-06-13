#include "admin_handler.hpp"
#include "auth_handler.hpp"
#include "../service/problem_service.hpp"
#include "../service/auth_service.hpp"
#include "../model/user.hpp"

#include <httplib.h>
#include <json.hpp>

using json = nlohmann::json;

static json json_error(const std::string& msg) {
    json j;
    j["error"] = msg;
    return j;
}

static bool require_admin(const httplib::Request& req) {
    User user;
    auto sid = get_session_id(req);
    return AuthService::instance().authenticate(sid, user) && user.role == "admin";
}

void handle_get_testcases(const httplib::Request& req, httplib::Response& res) {
    if (!require_admin(req)) {
        res.status = 403;
        res.set_content(json_error("Forbidden").dump(), "application/json");
        return;
    }

    int problem_id = std::stoi(req.path_params.at("id"));

    auto cases = ProblemService::instance().get_testcases(problem_id, true);

    json j = json::array();
    for (auto& tc : cases) {
        json item;
        item["id"] = tc.id;
        item["problem_id"] = tc.problem_id;
        item["input"] = tc.input;
        item["expected"] = tc.expected;
        item["is_sample"] = tc.is_sample;
        item["sort_order"] = tc.sort_order;
        j.push_back(std::move(item));
    }

    res.set_content(j.dump(), "application/json");
}

void handle_add_testcase(const httplib::Request& req, httplib::Response& res) {
    if (!require_admin(req)) {
        res.status = 403;
        res.set_content(json_error("Forbidden").dump(), "application/json");
        return;
    }

    int problem_id = std::stoi(req.path_params.at("id"));

    json body;
    try { body = json::parse(req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(json_error("Invalid JSON").dump(), "application/json");
        return;
    }

    int tc_id = ProblemService::instance().add_testcase(
        problem_id,
        body.value("input", ""),
        body.value("expected", ""),
        body.value("is_sample", false),
        body.value("sort_order", 0));

    if (tc_id < 0) {
        res.status = 500;
        res.set_content(json_error("Failed to add test case").dump(), "application/json");
        return;
    }

    json j;
    j["id"] = tc_id;
    j["message"] = "Test case added";
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

void handle_delete_testcase(const httplib::Request& req, httplib::Response& res) {
    if (!require_admin(req)) {
        res.status = 403;
        res.set_content(json_error("Forbidden").dump(), "application/json");
        return;
    }

    int tc_id = std::stoi(req.path_params.at("tc_id"));

    if (!ProblemService::instance().delete_testcase(tc_id)) {
        res.status = 500;
        res.set_content(json_error("Failed to delete test case").dump(), "application/json");
        return;
    }

    json j;
    j["message"] = "Test case deleted";
    res.set_content(j.dump(), "application/json");
}
