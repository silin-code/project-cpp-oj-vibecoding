#include "problem_handler.hpp"
#include "auth_handler.hpp"
#include "../service/problem_service.hpp"
#include "../model/problem.hpp"

#include <httplib.h>
#include <json.hpp>

using json = nlohmann::json;

static json json_error(const std::string& msg) {
    json j;
    j["error"] = msg;
    return j;
}

static bool is_admin(const httplib::Request& req) {
    auto user = require_auth(req);
    return user.role == "admin";
}

void handle_get_problems(const httplib::Request& req, httplib::Response& res) {
    auto problems = ProblemService::instance().get_problem_list();

    json j = json::array();
    for (auto& p : problems) {
        json item;
        item["id"] = p.id;
        item["title"] = p.title;
        item["difficulty"] = p.difficulty;
        item["pass_rate"] = p.pass_rate;
        item["total_submissions"] = p.total_submissions;
        item["accepted"] = p.accepted;
        j.push_back(std::move(item));
    }

    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void handle_get_problem(const httplib::Request& req, httplib::Response& res) {
    int problem_id = std::stoi(req.path_params.at("id"));

    auto p = ProblemService::instance().get_problem_detail(problem_id);
    if (p.id == 0) {
        res.status = 404;
        res.set_content(json_error("Problem not found").dump(), "application/json");
        return;
    }

    json j;
    j["id"] = p.id;
    j["title"] = p.title;
    j["description"] = p.description;
    j["input_desc"] = p.input_desc;
    j["output_desc"] = p.output_desc;
    j["difficulty"] = p.difficulty;
    j["time_limit"] = p.time_limit;
    j["memory_limit"] = p.memory_limit;
    j["created_at"] = p.created_at;
    j["updated_at"] = p.updated_at;

    json samples = json::array();
    for (auto& tc : p.sample_cases) {
        json s;
        s["input"] = tc.input;
        s["expected"] = tc.expected;
        s["sort_order"] = tc.sort_order;
        samples.push_back(std::move(s));
    }
    j["sample_cases"] = std::move(samples);

    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void handle_create_problem(const httplib::Request& req, httplib::Response& res) {
    if (!is_admin(req)) {
        res.status = 403;
        res.set_content(json_error("Forbidden").dump(), "application/json");
        return;
    }

    json body;
    try { body = json::parse(req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(json_error("Invalid JSON").dump(), "application/json");
        return;
    }

    int id = ProblemService::instance().create_problem(
        body.value("title", ""),
        body.value("description", ""),
        body.value("input_desc", ""),
        body.value("output_desc", ""),
        body.value("difficulty", "easy"),
        body.value("time_limit", 2),
        body.value("memory_limit", 256));

    if (id < 0) {
        res.status = 500;
        res.set_content(json_error("Failed to create problem").dump(), "application/json");
        return;
    }

    json j;
    j["id"] = id;
    j["message"] = "Problem created";
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

void handle_update_problem(const httplib::Request& req, httplib::Response& res) {
    if (!is_admin(req)) {
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

    bool ok = ProblemService::instance().update_problem(
        problem_id,
        body.value("title", ""),
        body.value("description", ""),
        body.value("input_desc", ""),
        body.value("output_desc", ""),
        body.value("difficulty", "easy"),
        body.value("time_limit", 2),
        body.value("memory_limit", 256));

    if (!ok) {
        res.status = 500;
        res.set_content(json_error("Failed to update problem").dump(), "application/json");
        return;
    }

    json j;
    j["message"] = "Problem updated";
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void handle_delete_problem(const httplib::Request& req, httplib::Response& res) {
    if (!is_admin(req)) {
        res.status = 403;
        res.set_content(json_error("Forbidden").dump(), "application/json");
        return;
    }

    int problem_id = std::stoi(req.path_params.at("id"));

    if (!ProblemService::instance().delete_problem(problem_id)) {
        res.status = 500;
        res.set_content(json_error("Failed to delete problem").dump(), "application/json");
        return;
    }

    json j;
    j["message"] = "Problem deleted";
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}
