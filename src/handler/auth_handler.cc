#include "auth_handler.hpp"
#include "../service/auth_service.hpp"
#include "../model/user.hpp"

#include <httplib.h>
#include <json.hpp>

using json = nlohmann::json;

static json json_error(const std::string& msg, int code = 0) {
    json j;
    j["error"] = msg;
    if (code > 0) j["code"] = code;
    return j;
}

std::string get_session_id(const httplib::Request& req) {
    auto cookie = req.get_header_value("Cookie");
    auto pos = cookie.find("session_id=");
    if (pos == std::string::npos) return "";

    pos += 11;
    auto end = cookie.find(';', pos);
    if (end == std::string::npos) {
        return cookie.substr(pos);
    }
    return cookie.substr(pos, end - pos);
}

static bool require_auth(const httplib::Request& req, User& user) {
    auto sid = get_session_id(req);
    return AuthService::instance().authenticate(sid, user);
}

void handle_register(const httplib::Request& req, httplib::Response& res) {
    json body;
    try { body = json::parse(req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(json_error("Invalid JSON").dump(), "application/json");
        return;
    }

    auto username = body.value("username", "");
    auto password = body.value("password", "");

    if (username.empty() || password.empty()) {
        res.status = 400;
        res.set_content(json_error("Username and password required").dump(), "application/json");
        return;
    }

    int user_id = AuthService::instance().register_user(username, password);
    if (user_id < 0) {
        res.status = 409;
        res.set_content(json_error("Username already exists").dump(), "application/json");
        return;
    }

    json j;
    j["id"] = user_id;
    j["username"] = username;
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

void handle_login(const httplib::Request& req, httplib::Response& res) {
    json body;
    try { body = json::parse(req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(json_error("Invalid JSON").dump(), "application/json");
        return;
    }

    auto username = body.value("username", "");
    auto password = body.value("password", "");

    if (username.empty() || password.empty()) {
        res.status = 400;
        res.set_content(json_error("Username and password required").dump(), "application/json");
        return;
    }

    auto session_id = AuthService::instance().login(username, password);
    if (session_id.empty()) {
        res.status = 401;
        res.set_content(json_error("Invalid credentials").dump(), "application/json");
        return;
    }

    json j;
    j["message"] = "Login successful";
    j["username"] = username;
    res.set_header("Set-Cookie", "session_id=" + session_id + "; Path=/; HttpOnly");
    res.set_content(j.dump(), "application/json");
}

void handle_logout(const httplib::Request& req, httplib::Response& res) {
    auto sid = get_session_id(req);
    AuthService::instance().logout(sid);

    json j;
    j["message"] = "Logged out";
    res.set_header("Set-Cookie", "session_id=; Path=/; Max-Age=0");
    res.set_content(j.dump(), "application/json");
}

void handle_me(const httplib::Request& req, httplib::Response& res) {
    User user;
    if (!require_auth(req, user)) {
        res.status = 401;
        res.set_content(json_error("Not authenticated").dump(), "application/json");
        return;
    }

    json j;
    j["id"] = user.id;
    j["username"] = user.username;
    j["role"] = user.role;
    res.set_content(j.dump(), "application/json");
}
