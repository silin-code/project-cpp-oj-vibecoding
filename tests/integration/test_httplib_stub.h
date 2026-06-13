#pragma once
#ifndef OJ_TEST_HTTP_STUB_H
#define OJ_TEST_HTTP_STUB_H

#include <map>
#include <string>

namespace httplib {

struct Request {
    std::string body;
    std::map<std::string, std::string> path_params;
    std::multimap<std::string, std::string> params;
    std::multimap<std::string, std::string> headers;

    std::string get_header_value(const std::string& key, const std::string& def = "") const {
        auto it = headers.find(key);
        return it != headers.end() ? it->second : def;
    }

    std::string get_param_value(const std::string& key, const std::string& def = "") const {
        auto it = params.find(key);
        return it != params.end() ? it->second : def;
    }
};

struct Response {
    int status = 200;
    std::string body;
    std::multimap<std::string, std::string> headers;

    void set_content(const std::string& content, const std::string& content_type) {
        body = content;
        headers.emplace("Content-Type", content_type);
    }

    void set_header(const std::string& key, const std::string& value) {
        headers.emplace(key, value);
    }
};

} // namespace httplib

#endif // OJ_TEST_HTTP_STUB_H
