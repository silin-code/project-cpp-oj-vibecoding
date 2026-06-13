#pragma once

namespace httplib {
struct Request;
struct Response;
}

void handle_get_problems(const httplib::Request& req, httplib::Response& res);
void handle_get_problem(const httplib::Request& req, httplib::Response& res);
void handle_create_problem(const httplib::Request& req, httplib::Response& res);
void handle_update_problem(const httplib::Request& req, httplib::Response& res);
void handle_delete_problem(const httplib::Request& req, httplib::Response& res);
