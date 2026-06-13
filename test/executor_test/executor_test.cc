#include <gtest/gtest.h>
#include <cstdio>
#include <ctime>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>

#include "service/executor_service.hpp"
#include "service/problem_service.hpp"
#include "db/connection_pool.hpp"
#include "utils/config.hpp"
#include "utils/logger.hpp"
#include "model/problem.hpp"
#include "model/test_case.hpp"

namespace fs = std::filesystem;

class ExecutorTest : public ::testing::Test {
protected:
    int problem_id = -1;
    int test_case_id = -1;
    int submission_id = -1;
    std::string test_title;

    void SetUp() override {
        test_title = "Executor Test " + std::to_string(std::time(nullptr));

        setenv("PATH", (std::string(getenv("PATH")) + ":/usr/bin:/bin").c_str(), 1);

        Config::instance().load("config/config.json");
        Logger::instance().init("/tmp/oj_test_executor.log", LogLevel::DEBUG);
        ConnectionPool::instance().init(Config::instance().database());
        ProblemService::instance().init();
    }

    void TearDown() override {
        if (submission_id > 0) {
            auto* conn = ConnectionPool::instance().get();
            if (conn) {
                std::string q = "DELETE FROM submissions WHERE id = " + std::to_string(submission_id);
                mysql_query(conn, q.c_str());
                ConnectionPool::instance().release(conn);
            }
            submission_id = -1;
        }
        if (problem_id > 0) {
            auto* conn = ConnectionPool::instance().get();
            if (conn) {
                std::string q = "DELETE FROM problems WHERE id = " + std::to_string(problem_id);
                mysql_query(conn, q.c_str());
                ConnectionPool::instance().release(conn);
            }
            problem_id = -1;
        }
        ConnectionPool::instance().close_all();
    }

    int create_test_problem(int time_limit = 2, int mem_limit = 256) {
        problem_id = ProblemService::instance().create_problem(
            test_title, "Executor test problem", "1 2", "3",
            "easy", time_limit, mem_limit);
        return problem_id;
    }

    int add_test_case(const std::string& input, const std::string& expected, bool is_sample = true) {
        test_case_id = ProblemService::instance().add_testcase(
            problem_id, input, expected, is_sample, 0);
        return test_case_id;
    }

    int create_submission(const std::string& code) {
        auto* conn = ConnectionPool::instance().get();
        if (!conn) return -1;
        std::vector<char> buf(code.size() * 2 + 1);
        mysql_real_escape_string(conn, buf.data(), code.data(), code.size());
        std::string esc_code(buf.data());
        std::string q = "INSERT INTO submissions (user_id, problem_id, code, status) VALUES (1, "
                        + std::to_string(problem_id) + ", '" + esc_code + "', 'PENDING')";
        if (mysql_query(conn, q.c_str()) != 0) {
            ConnectionPool::instance().release(conn);
            return -1;
        }
        submission_id = mysql_insert_id(conn);
        ConnectionPool::instance().release(conn);
        return submission_id;
    }

    std::string get_submission_status() {
        auto* conn = ConnectionPool::instance().get();
        if (!conn) return "";
        std::string q = "SELECT status FROM submissions WHERE id=" + std::to_string(submission_id);
        std::string status;
        if (mysql_query(conn, q.c_str()) == 0) {
            auto* result = mysql_store_result(conn);
            if (result) {
                auto* row = mysql_fetch_row(result);
                if (row && row[0]) status = row[0];
                mysql_free_result(result);
            }
        }
        ConnectionPool::instance().release(conn);
        return status;
    }

    std::string get_submission_error() {
        auto* conn = ConnectionPool::instance().get();
        if (!conn) return "";
        std::string q = "SELECT error_msg FROM submissions WHERE id=" + std::to_string(submission_id);
        std::string msg;
        if (mysql_query(conn, q.c_str()) == 0) {
            auto* result = mysql_store_result(conn);
            if (result) {
                auto* row = mysql_fetch_row(result);
                if (row && row[0]) msg = row[0];
                mysql_free_result(result);
            }
        }
        ConnectionPool::instance().release(conn);
        return msg;
    }

    int get_failed_case() {
        auto* conn = ConnectionPool::instance().get();
        if (!conn) return 0;
        std::string q = "SELECT failed_case FROM submissions WHERE id=" + std::to_string(submission_id);
        int fc = 0;
        if (mysql_query(conn, q.c_str()) == 0) {
            auto* result = mysql_store_result(conn);
            if (result) {
                auto* row = mysql_fetch_row(result);
                if (row && row[0]) fc = std::stoi(row[0]);
                mysql_free_result(result);
            }
        }
        ConnectionPool::instance().release(conn);
        return fc;
    }
};

TEST_F(ExecutorTest, TrimOutputBasic) {
    auto& exec = ExecutorService::instance();
    auto trim = [&](const std::string& s) {
        return exec.trim_output(s);
    };

    EXPECT_EQ(trim("hello"), "hello");
    EXPECT_EQ(trim("  hello  "), "hello");
    EXPECT_EQ(trim("hello\n"), "hello");
    EXPECT_EQ(trim("hello\nworld"), "hello\nworld");
    EXPECT_EQ(trim("  hello  \n  world  "), "hello\nworld");
    EXPECT_EQ(trim(""), "");
    EXPECT_EQ(trim("\n"), "");
    EXPECT_EQ(trim("  \n  "), "");
    EXPECT_EQ(trim("\n\n"), "");
    EXPECT_EQ(trim("a\n\nb"), "a\n\nb");
}

TEST_F(ExecutorTest, TrimOutputTrailingSpacesOnLines) {
    auto& exec = ExecutorService::instance();

    EXPECT_EQ(exec.trim_output("1  "), "1");
    EXPECT_EQ(exec.trim_output("1   \n2   "), "1\n2");
    EXPECT_EQ(exec.trim_output("  1 2  "), "1 2");
}

TEST_F(ExecutorTest, CompileValidProgram) {
    std::string src = "/tmp/oj_test_executor_compile_ok.cpp";
    std::string bin = "/tmp/oj_test_executor_compile_ok.out";

    {
        std::ofstream f(src);
        f << "#include <iostream>\nint main() { std::cout << 42; }\n";
    }

    auto& exec = ExecutorService::instance();
    std::string err;
    bool ok = exec.compile(src, bin, err);
    EXPECT_TRUE(ok) << "Compile error: " << err;
    EXPECT_TRUE(fs::exists(bin));

    fs::remove(src);
    fs::remove(bin);
}

TEST_F(ExecutorTest, CompileInvalidProgram) {
    std::string src = "/tmp/oj_test_executor_compile_err.cpp";
    std::string bin = "/tmp/oj_test_executor_compile_err.out";

    {
        std::ofstream f(src);
        f << "this is not valid C++ code!!!\n";
    }

    auto& exec = ExecutorService::instance();
    std::string err;
    bool ok = exec.compile(src, bin, err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());

    fs::remove(src);
}

TEST_F(ExecutorTest, RunSandboxAC) {
    std::string bin = "/tmp/oj_test_executor_ac.out";
    std::string src = "/tmp/oj_test_executor_ac.cpp";

    {
        std::ofstream f(src);
        f << "#include <iostream>\nint main() {\n"
          << "  int a, b; std::cin >> a >> b;\n"
          << "  std::cout << a + b;\n"
          << "  return 0;\n}\n";
    }

    auto& exec = ExecutorService::instance();
    std::string err;
    ASSERT_TRUE(exec.compile(src, bin, err)) << err;

    std::string output, run_err;
    int time_ms = 0, mem_kb = 0;
    int ret = exec.run_sandbox(bin, "1 2", output, run_err, time_ms, mem_kb, 2, 256);
    EXPECT_EQ(ret, 0) << "run_sandbox returned " << ret << ": " << run_err;
    EXPECT_EQ(exec.trim_output(output), "3");
    EXPECT_GT(time_ms, 0);

    fs::remove(src);
    fs::remove(bin);
}

TEST_F(ExecutorTest, RunSandboxTLE) {
    std::string bin = "/tmp/oj_test_executor_tle.out";
    std::string src = "/tmp/oj_test_executor_tle.cpp";

    {
        std::ofstream f(src);
        f << "int main() { volatile int x = 0; while (1) { ++x; } return 0; }\n";
    }

    auto& exec = ExecutorService::instance();
    std::string err;
    ASSERT_TRUE(exec.compile(src, bin, err)) << err;

    std::string output, run_err;
    int time_ms = 0, mem_kb = 0;
    int ret = exec.run_sandbox(bin, "", output, run_err, time_ms, mem_kb, 1, 256);
    EXPECT_EQ(ret, 1);

    fs::remove(src);
    fs::remove(bin);
}

TEST_F(ExecutorTest, RunSandboxRE) {
    std::string bin = "/tmp/oj_test_executor_re.out";
    std::string src = "/tmp/oj_test_executor_re.cpp";

    {
        std::ofstream f(src);
        f << "#include <cstdlib>\nint main() { abort(); return 0; }\n";
    }

    auto& exec = ExecutorService::instance();
    std::string err;
    ASSERT_TRUE(exec.compile(src, bin, err)) << err;

    std::string output, run_err;
    int time_ms = 0, mem_kb = 0;
    int ret = exec.run_sandbox(bin, "", output, run_err, time_ms, mem_kb, 2, 256);
    EXPECT_EQ(ret, -1);
    EXPECT_FALSE(run_err.empty());

    fs::remove(src);
    fs::remove(bin);
}

TEST_F(ExecutorTest, RunSandboxWA) {
    std::string bin = "/tmp/oj_test_executor_wa.out";
    std::string src = "/tmp/oj_test_executor_wa.cpp";

    {
        std::ofstream f(src);
        f << "#include <iostream>\nint main() {\n"
          << "  int a, b; std::cin >> a >> b;\n"
          << "  std::cout << a * b;\n"
          << "  return 0;\n}\n";
    }

    auto& exec = ExecutorService::instance();
    std::string err;
    ASSERT_TRUE(exec.compile(src, bin, err)) << err;

    std::string output, run_err;
    int time_ms = 0, mem_kb = 0;
    int ret = exec.run_sandbox(bin, "1 2", output, run_err, time_ms, mem_kb, 2, 256);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(exec.trim_output(output), "2");

    fs::remove(src);
    fs::remove(bin);
}

TEST_F(ExecutorTest, RunSandboxEmptyOutput) {
    std::string bin = "/tmp/oj_test_executor_empty.out";
    std::string src = "/tmp/oj_test_executor_empty.cpp";

    {
        std::ofstream f(src);
        f << "int main() { return 0; }\n";
    }

    auto& exec = ExecutorService::instance();
    std::string err;
    ASSERT_TRUE(exec.compile(src, bin, err)) << err;

    std::string output, run_err;
    int time_ms = 0, mem_kb = 0;
    int ret = exec.run_sandbox(bin, "", output, run_err, time_ms, mem_kb, 2, 256);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(output, "");

    fs::remove(src);
    fs::remove(bin);
}

TEST_F(ExecutorTest, RunSandboxMultilineOutput) {
    std::string bin = "/tmp/oj_test_executor_ml.out";
    std::string src = "/tmp/oj_test_executor_ml.cpp";

    {
        std::ofstream f(src);
        f << "#include <iostream>\nint main() {\n"
          << "  std::cout << \"line1\\nline2\\nline3\";\n"
          << "  return 0;\n}\n";
    }

    auto& exec = ExecutorService::instance();
    std::string err;
    ASSERT_TRUE(exec.compile(src, bin, err)) << err;

    std::string output, run_err;
    int time_ms = 0, mem_kb = 0;
    int ret = exec.run_sandbox(bin, "", output, run_err, time_ms, mem_kb, 2, 256);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(output, "line1\nline2\nline3");

    fs::remove(src);
    fs::remove(bin);
}

TEST_F(ExecutorTest, EnqueueAndJudgeAC) {
    ExecutorService::instance().init();

    create_test_problem(5, 512);
    ASSERT_GT(problem_id, 0);
    add_test_case("1 2", "3", true);
    ASSERT_GT(test_case_id, 0);
    add_test_case("5 7", "12", true);
    ASSERT_GT(test_case_id, 0);

    std::string code =
        "#include <iostream>\n"
        "int main() {\n"
        "  int a, b;\n"
        "  std::cin >> a >> b;\n"
        "  std::cout << a + b;\n"
        "  return 0;\n"
        "}\n";

    int sid = create_submission(code);
    ASSERT_GT(sid, 0);

    bool queued = ExecutorService::instance().enqueue(
        sid, problem_id, 1, code, 5, 512);
    EXPECT_TRUE(queued);

    for (int i = 0; i < 30; i++) {
        auto status = get_submission_status();
        if (status != "PENDING" && status != "JUDGING") {
            EXPECT_EQ(status, "AC");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    ExecutorService::instance().shutdown();
}

TEST_F(ExecutorTest, EnqueueAndJudgeCE) {
    ExecutorService::instance().init();

    create_test_problem(5, 512);
    ASSERT_GT(problem_id, 0);
    add_test_case("1 2", "3", true);

    std::string code = "int main() { this is not c++; }";

    int sid = create_submission(code);
    ASSERT_GT(sid, 0);

    bool queued = ExecutorService::instance().enqueue(
        sid, problem_id, 1, code, 5, 512);
    EXPECT_TRUE(queued);

    for (int i = 0; i < 30; i++) {
        auto status = get_submission_status();
        if (status != "PENDING" && status != "JUDGING") {
            EXPECT_EQ(status, "CE");
            auto err = get_submission_error();
            EXPECT_FALSE(err.empty());
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    ExecutorService::instance().shutdown();
}

TEST_F(ExecutorTest, EnqueueAndJudgeWA) {
    ExecutorService::instance().init();

    create_test_problem(5, 512);
    ASSERT_GT(problem_id, 0);
    add_test_case("1 2", "3", true);
    add_test_case("4 5", "9", true);

    std::string code =
        "#include <iostream>\n"
        "int main() {\n"
        "  int a, b;\n"
        "  std::cin >> a >> b;\n"
        "  std::cout << a * b;\n"
        "  return 0;\n"
        "}\n";

    int sid = create_submission(code);
    ASSERT_GT(sid, 0);

    bool queued = ExecutorService::instance().enqueue(
        sid, problem_id, 1, code, 5, 512);
    EXPECT_TRUE(queued);

    for (int i = 0; i < 30; i++) {
        auto status = get_submission_status();
        if (status != "PENDING" && status != "JUDGING") {
            EXPECT_EQ(status, "WA");
            EXPECT_EQ(get_failed_case(), 1);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    ExecutorService::instance().shutdown();
}

TEST_F(ExecutorTest, TrimOutputSpecialErrorsTest) {
    ExecutorService::instance().init();

    create_test_problem(5, 512);
    ASSERT_GT(problem_id, 0);
    add_test_case("1 2", "3", true);

    std::string code =
        "#include <iostream>\n"
        "int main() {\n"
        "  int a, b;\n"
        "  std::cin >> a >> b;\n"
        "  std::cout << a + b;\n"
        "  return 0;\n"
        "}\n";

    int sid = create_submission(code);
    ASSERT_GT(sid, 0);

    bool queued = ExecutorService::instance().enqueue(
        sid, problem_id, 1, code, 5, 512);
    EXPECT_TRUE(queued);

    for (int i = 0; i < 30; i++) {
        auto status = get_submission_status();
        if (status != "PENDING" && status != "JUDGING") {
            EXPECT_EQ(status, "AC");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    ExecutorService::instance().shutdown();
}
