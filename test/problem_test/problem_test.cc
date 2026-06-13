#include <gtest/gtest.h>
#include <ctime>
#include <string>

#include "service/problem_service.hpp"
#include "model/problem.hpp"
#include "model/test_case.hpp"
#include "utils/config.hpp"
#include "utils/logger.hpp"
#include "db/connection_pool.hpp"

class ProblemTest : public ::testing::Test {
protected:
    std::string test_title;
    std::string test_desc = "Test description";
    std::string test_input = "1 2";
    std::string test_output = "3";
    std::string test_diff = "easy";
    int test_time = 2;
    int test_mem = 256;
    int problem_id = -1;

    void SetUp() override {
        test_title = "Test Problem " + std::to_string(std::time(nullptr));

        Config::instance().load("config/config.json");
        Logger::instance().init("/tmp/oj_test_problem.log", LogLevel::DEBUG);
        ConnectionPool::instance().init(Config::instance().database());
        ProblemService::instance().init();
    }

    void TearDown() override {
        if (problem_id > 0) {
            auto* conn = ConnectionPool::instance().get();
            if (conn) {
                std::string q = "DELETE FROM problems WHERE id = " + std::to_string(problem_id);
                mysql_query(conn, q.c_str());
                ConnectionPool::instance().release(conn);
            }
        }
        ConnectionPool::instance().close_all();
    }
};

TEST_F(ProblemTest, CreateProblem) {
    problem_id = ProblemService::instance().create_problem(
        test_title, test_desc, test_input, test_output,
        test_diff, test_time, test_mem);
    EXPECT_GT(problem_id, 0);
}

TEST_F(ProblemTest, CreateProblemAndExists) {
    problem_id = ProblemService::instance().create_problem(
        test_title, test_desc, test_input, test_output,
        test_diff, test_time, test_mem);
    ASSERT_GT(problem_id, 0);
    EXPECT_TRUE(ProblemService::instance().problem_exists(problem_id));
    EXPECT_FALSE(ProblemService::instance().problem_exists(999999));
}

TEST_F(ProblemTest, GetProblemList) {
    problem_id = ProblemService::instance().create_problem(
        test_title, test_desc, test_input, test_output,
        test_diff, test_time, test_mem);
    ASSERT_GT(problem_id, 0);

    auto problems = ProblemService::instance().get_problem_list();
    bool found = false;
    for (const auto& p : problems) {
        if (p.id == problem_id) {
            found = true;
            EXPECT_EQ(p.title, test_title);
            EXPECT_EQ(p.difficulty, test_diff);
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ProblemTest, GetProblemDetail) {
    problem_id = ProblemService::instance().create_problem(
        test_title, test_desc, test_input, test_output,
        test_diff, test_time, test_mem);
    ASSERT_GT(problem_id, 0);

    auto p = ProblemService::instance().get_problem_detail(problem_id);
    EXPECT_EQ(p.title, test_title);
    EXPECT_EQ(p.description, test_desc);
    EXPECT_EQ(p.input_desc, test_input);
    EXPECT_EQ(p.output_desc, test_output);
    EXPECT_EQ(p.difficulty, test_diff);
    EXPECT_EQ(p.time_limit, test_time);
    EXPECT_EQ(p.memory_limit, test_mem);
}

TEST_F(ProblemTest, GetProblemDetailNonExistent) {
    auto p = ProblemService::instance().get_problem_detail(999999);
    EXPECT_EQ(p.id, 0);
}

TEST_F(ProblemTest, UpdateProblem) {
    problem_id = ProblemService::instance().create_problem(
        test_title, test_desc, test_input, test_output,
        test_diff, test_time, test_mem);
    ASSERT_GT(problem_id, 0);

    bool ok = ProblemService::instance().update_problem(
        problem_id, "Updated Title", "Updated desc",
        "2 2", "4", "hard", 5, 512);
    EXPECT_TRUE(ok);

    auto p = ProblemService::instance().get_problem_detail(problem_id);
    EXPECT_EQ(p.title, "Updated Title");
    EXPECT_EQ(p.description, "Updated desc");
    EXPECT_EQ(p.input_desc, "2 2");
    EXPECT_EQ(p.output_desc, "4");
    EXPECT_EQ(p.difficulty, "hard");
    EXPECT_EQ(p.time_limit, 5);
    EXPECT_EQ(p.memory_limit, 512);
}

TEST_F(ProblemTest, DeleteProblem) {
    problem_id = ProblemService::instance().create_problem(
        test_title, test_desc, test_input, test_output,
        test_diff, test_time, test_mem);
    ASSERT_GT(problem_id, 0);

    bool ok = ProblemService::instance().delete_problem(problem_id);
    EXPECT_TRUE(ok);

    EXPECT_FALSE(ProblemService::instance().problem_exists(problem_id));
    problem_id = -1;  // prevent TearDown double-delete
}

TEST_F(ProblemTest, AddAndGetTestcases) {
    problem_id = ProblemService::instance().create_problem(
        test_title, test_desc, test_input, test_output,
        test_diff, test_time, test_mem);
    ASSERT_GT(problem_id, 0);

    int tc1 = ProblemService::instance().add_testcase(
        problem_id, "1 2", "3", true, 0);
    EXPECT_GT(tc1, 0);

    int tc2 = ProblemService::instance().add_testcase(
        problem_id, "4 5", "9", false, 1);
    EXPECT_GT(tc2, 0);

    auto all = ProblemService::instance().get_testcases(problem_id, true);
    EXPECT_EQ(all.size(), 2);

    auto samples = ProblemService::instance().get_testcases(problem_id, false);
    EXPECT_EQ(samples.size(), 1);
}

TEST_F(ProblemTest, DeleteTestcase) {
    problem_id = ProblemService::instance().create_problem(
        test_title, test_desc, test_input, test_output,
        test_diff, test_time, test_mem);
    ASSERT_GT(problem_id, 0);

    int tc = ProblemService::instance().add_testcase(
        problem_id, "1 2", "3", true, 0);
    ASSERT_GT(tc, 0);

    bool ok = ProblemService::instance().delete_testcase(tc);
    EXPECT_TRUE(ok);

    auto cases = ProblemService::instance().get_testcases(problem_id, true);
    EXPECT_EQ(cases.size(), 0);
}
