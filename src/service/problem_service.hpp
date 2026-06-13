#pragma once

#include <string>
#include <vector>

struct Problem;
struct TestCase;

class ProblemService {
public:
    static ProblemService& instance();

    bool init();

    std::vector<Problem> get_problem_list();
    Problem get_problem_detail(int problem_id);
    bool problem_exists(int problem_id);

    int create_problem(const std::string& title, const std::string& description,
                       const std::string& input_desc, const std::string& output_desc,
                       const std::string& difficulty, int time_limit, int memory_limit);
    bool update_problem(int problem_id, const std::string& title,
                        const std::string& description, const std::string& input_desc,
                        const std::string& output_desc, const std::string& difficulty,
                        int time_limit, int memory_limit);
    bool delete_problem(int problem_id);

    std::vector<TestCase> get_testcases(int problem_id, bool include_hidden = false);
    int add_testcase(int problem_id, const std::string& input,
                     const std::string& expected, bool is_sample, int sort_order);
    bool delete_testcase(int tc_id);

private:
    ProblemService() = default;
    ~ProblemService() = default;
    ProblemService(const ProblemService&) = delete;
    ProblemService& operator=(const ProblemService&) = delete;
};
