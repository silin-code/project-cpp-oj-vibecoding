#pragma once

#include <string>
#include <vector>
#include <optional>

struct TestCase;

struct Problem {
    int id = 0;
    std::string title;
    std::string description;
    std::string input_desc;
    std::string output_desc;
    std::string difficulty = "easy";
    int time_limit = 2;
    int memory_limit = 256;
    std::string created_at;
    std::string updated_at;

    std::vector<TestCase> sample_cases;
    double pass_rate = 0.0;
    int total_submissions = 0;
    int accepted = 0;
};
