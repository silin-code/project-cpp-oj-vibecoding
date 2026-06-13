#include <gtest/gtest.h>
#include <cstdio>
#include <ctime>
#include <string>

#include <httplib.h>
#include <json.hpp>

#include "handler/submit_handler.hpp"
#include "handler/auth_handler.hpp"
#include "service/auth_service.hpp"
#include "service/problem_service.hpp"
#include "service/executor_service.hpp"
#include "utils/config.hpp"
#include "utils/logger.hpp"
#include "db/connection_pool.hpp"

using json = nlohmann::json;

class SubmitApiTest : public ::testing::Test {
protected:
    std::string test_username;
    std::string test_password = "test_pass_123";
    std::string test_session;
    int test_user_id = -1;
    int test_problem_id = -1;

    void SetUp() override {
        auto ts = std::to_string(std::time(nullptr));
        test_username = "submitapi_" + ts;

        Config::instance().load("config/config.json");
        Logger::instance().init("/tmp/oj_test_submit_api.log", LogLevel::DEBUG);
        ConnectionPool::instance().init(Config::instance().database());
        AuthService::instance().init();
        ProblemService::instance().init();
        ExecutorService::instance().init();

        std::string err;
        test_user_id = AuthService::instance().register_user(test_username, test_password, err);
        ASSERT_GT(test_user_id, 0);

        test_session = AuthService::instance().login(test_username, test_password, err);
        ASSERT_FALSE(test_session.empty());

        test_problem_id = ProblemService::instance().create_problem(
            "Submit Test Problem", "Description", "Input", "Output",
            "easy", 2, 256);
        ASSERT_GT(test_problem_id, 0);

        ProblemService::instance().add_testcase(test_problem_id, "1 2", "3", true, 0);
    }

    void TearDown() override {
        ExecutorService::instance().shutdown();
        AuthService::instance().shutdown();

        auto* conn = ConnectionPool::instance().get();
        if (conn) {
            std::string q = "DELETE FROM submissions WHERE user_id = " + std::to_string(test_user_id);
            mysql_query(conn, q.c_str());
            if (test_problem_id > 0) {
                q = "DELETE FROM problems WHERE id = " + std::to_string(test_problem_id);
                mysql_query(conn, q.c_str());
            }
            q = "DELETE FROM users WHERE username ='" + test_username + "'";
            mysql_query(conn, q.c_str());
            ConnectionPool::instance().release(conn);
        }
        ConnectionPool::instance().close_all();
    }

    httplib::Request make_auth_req() {
        httplib::Request req;
        req.headers.emplace("Cookie", "session_id=" + test_session);
        return req;
    }

    int submit_code(const std::string& code, int expected_status) {
        httplib::Request req = make_auth_req();
        httplib::Response res;

        json body;
        body["problem_id"] = test_problem_id;
        body["code"] = code;
        req.body = body.dump();

        handle_submit(req, res);
        EXPECT_EQ(res.status, expected_status);

        if (expected_status == 202) {
            auto j = json::parse(res.body);
            return j["submission_id"];
        }
        return -1;
    }
};

TEST_F(SubmitApiTest, SubmitSuccessReturns202) {
    int sid = submit_code(
        "#include <iostream>\nint main() { int a,b; std::cin>>a>>b; std::cout<<a+b; return 0; }",
        202);
    EXPECT_GT(sid, 0);
}

TEST_F(SubmitApiTest, SubmitUnauthenticated) {
    httplib::Request req;
    httplib::Response res;

    json body;
    body["problem_id"] = test_problem_id;
    body["code"] = "int main(){}";
    req.body = body.dump();

    handle_submit(req, res);
    EXPECT_EQ(res.status, 401);
}

TEST_F(SubmitApiTest, SubmitMissingProblemId) {
    httplib::Request req = make_auth_req();
    httplib::Response res;

    json body;
    body["code"] = "int main(){}";
    req.body = body.dump();

    handle_submit(req, res);
    EXPECT_EQ(res.status, 400);
}

TEST_F(SubmitApiTest, SubmitEmptyCode) {
    httplib::Request req = make_auth_req();
    httplib::Response res;

    json body;
    body["problem_id"] = test_problem_id;
    body["code"] = "";
    req.body = body.dump();

    handle_submit(req, res);
    EXPECT_EQ(res.status, 400);
}

TEST_F(SubmitApiTest, SubmitProblemNotFound) {
    httplib::Request req = make_auth_req();
    httplib::Response res;

    json body;
    body["problem_id"] = 999999;
    body["code"] = "int main(){}";
    req.body = body.dump();

    handle_submit(req, res);
    EXPECT_EQ(res.status, 404);
}

TEST_F(SubmitApiTest, SubmitInvalidJson) {
    httplib::Request req = make_auth_req();
    httplib::Response res;
    req.body = "not json";

    handle_submit(req, res);
    EXPECT_EQ(res.status, 400);
}

TEST_F(SubmitApiTest, GetSubmissionById) {
    int sid = submit_code(
        "#include <iostream>\nint main() { int a,b; std::cin>>a>>b; std::cout<<a+b; return 0; }",
        202);
    ASSERT_GT(sid, 0);

    httplib::Request req = make_auth_req();
    req.path_params["id"] = std::to_string(sid);
    httplib::Response res;

    handle_get_submission(req, res);

    EXPECT_EQ(res.status, 200);
    auto j = json::parse(res.body);
    EXPECT_EQ(j["id"], sid);
    EXPECT_EQ(j["problem_id"], test_problem_id);
    EXPECT_EQ(j["status"], "PENDING");
}

TEST_F(SubmitApiTest, GetSubmissionByIdNotFound) {
    httplib::Request req = make_auth_req();
    req.path_params["id"] = "999999";
    httplib::Response res;

    handle_get_submission(req, res);
    EXPECT_EQ(res.status, 404);
}

TEST_F(SubmitApiTest, GetSubmissionByIdUnauthenticated) {
    httplib::Request req;
    req.path_params["id"] = "1";
    httplib::Response res;

    handle_get_submission(req, res);
    EXPECT_EQ(res.status, 401);
}

TEST_F(SubmitApiTest, GetSubmissionsList) {
    submit_code(
        "#include <iostream>\nint main() { int a,b; std::cin>>a>>b; std::cout<<a+b; return 0; }",
        202);

    httplib::Request req = make_auth_req();
    httplib::Response res;

    handle_get_submissions(req, res);

    EXPECT_EQ(res.status, 200);
    auto arr = json::parse(res.body);
    EXPECT_TRUE(arr.is_array());
    EXPECT_GT(arr.size(), 0);
}

TEST_F(SubmitApiTest, GetSubmissionsFilterByProblem) {
    submit_code(
        "#include <iostream>\nint main() { int a,b; std::cin>>a>>b; std::cout<<a+b; return 0; }",
        202);

    httplib::Request req = make_auth_req();
    req.params.emplace("problem_id", std::to_string(test_problem_id));
    httplib::Response res;

    handle_get_submissions(req, res);

    EXPECT_EQ(res.status, 200);
    auto arr = json::parse(res.body);
    EXPECT_TRUE(arr.is_array());
    EXPECT_GT(arr.size(), 0);
    for (auto& item : arr) {
        EXPECT_EQ(item["problem_id"], test_problem_id);
    }
}

TEST_F(SubmitApiTest, GetSubmissionsEmpty) {
    httplib::Request req = make_auth_req();
    req.params.emplace("problem_id", "999999");
    httplib::Response res;

    handle_get_submissions(req, res);

    EXPECT_EQ(res.status, 200);
    auto arr = json::parse(res.body);
    EXPECT_TRUE(arr.is_array());
}

TEST_F(SubmitApiTest, GetSubmissionsUnauthenticated) {
    httplib::Request req;
    httplib::Response res;

    handle_get_submissions(req, res);
    EXPECT_EQ(res.status, 401);
}

TEST_F(SubmitApiTest, SubmitRateLimit) {
    int sid = submit_code(
        "#include <iostream>\nint main() { int a,b; std::cin>>a>>b; std::cout<<a+b; return 0; }",
        202);
    ASSERT_GT(sid, 0);

    httplib::Request req = make_auth_req();
    httplib::Response res;

    json body;
    body["problem_id"] = test_problem_id;
    body["code"] = "int main(){}";
    req.body = body.dump();

    handle_submit(req, res);
    EXPECT_EQ(res.status, 429);
    auto j = json::parse(res.body);
    EXPECT_FALSE(std::string(j["error"]).empty());
}
