#pragma once

#include <string>
#include "service/auth_service.hpp"

namespace httplib {
struct Request;
struct Response;
}

void handle_register(const httplib::Request& req, httplib::Response& res);
void handle_login(const httplib::Request& req, httplib::Response& res);
void handle_logout(const httplib::Request& req, httplib::Response& res);
void handle_me(const httplib::Request& req, httplib::Response& res);

std::string get_session_id(const httplib::Request& req);
SessionInfo require_auth(const httplib::Request& req);
