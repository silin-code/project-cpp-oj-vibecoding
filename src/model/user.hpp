#pragma once

#include <string>

struct User {
    int id = 0;
    std::string username;
    std::string password;
    std::string role = "user";
    std::string created_at;
};
