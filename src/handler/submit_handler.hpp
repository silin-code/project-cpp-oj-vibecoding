#pragma once

namespace httplib {
struct Request;
struct Response;
}

void handle_submit(const httplib::Request& req, httplib::Response& res);
void handle_get_submission(const httplib::Request& req, httplib::Response& res);
void handle_get_submissions(const httplib::Request& req, httplib::Response& res);
