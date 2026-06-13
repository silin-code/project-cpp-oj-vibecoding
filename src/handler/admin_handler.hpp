#pragma once

namespace httplib {
struct Request;
struct Response;
}

void handle_get_testcases(const httplib::Request& req, httplib::Response& res);
void handle_add_testcase(const httplib::Request& req, httplib::Response& res);
void handle_delete_testcase(const httplib::Request& req, httplib::Response& res);
