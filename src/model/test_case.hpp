#pragma once

#include <string>

struct TestCase {
    int id = 0;
    int problem_id = 0;
    std::string input;
    std::string expected;
    bool is_sample = false;
    int sort_order = 0;
};
