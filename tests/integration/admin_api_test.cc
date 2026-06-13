#include <gtest/gtest.h>
#include <cstdio>
#include <ctime>
#include <string>

#include <httplib.h>
#include <json.hpp>

#include "handler/admin_handler.hpp"
#include "handler/auth_handler.hpp"
#include "service/problem_service.hpp"
#include "service/auth_service.hpp"
#include "utils/config.hpp"
#include "utils/logger.hpp"
#include "db/connection_pool.hpp"

using json = nlohmann::json;

class AdminApiTest : public ::testing::Test {
protected:
    std::string admin_username;
    std::string admin_password = "admin_pass_123";
    std::string admin_session;
    int test_problem_id = -1;

    void SetUp() override {
        auto ts = std::to_string(std::time(nullptr));
        admin_username = "adminapi_" + ts;

        Config::instance().load("config/config.json");
        Logger::instance().init("/tmp/oj_test_admin_api.log", LogLevel::DEBUG);
        ConnectionPool::instance().init(Config::instance().database());
        AuthService::instance().init();
        ProblemService::instance().init();

        std::string err;
        int uid = AuthService::instance().register_user(admin_username, admin_password, err);
        ASSERT_GT(uid, 0);

        auto* conn = ConnectionPool::instance().get();
        ASSERT_NE(conn, nullptr);
        std::string q = "UPDATE users SET role='admin' WHERE username='" + admin_username + "'";
        mysql_query(conn, q.c_str());
        ConnectionPool::instance().release(conn);

        admin_session = AuthService::instance().login(admin_username, admin_password, err);
        ASSERT_FALSE(admin_session.empty());

        test_problem_id = ProblemService::instance().create_problem(
            "TC Test Problem", "Description", "Input", "Output", "easy", 2, 256);
        ASSERT_GT(test_problem_id, 0);
    }

    void TearDown() override {
        AuthService::instance().shutdown();

        auto* conn = ConnectionPool::instance().get();
        if (conn) {
            if (test_problem_id > 0) {
                std::string q = "DELETE FROM problems WHERE id = " + std::to_string(test_problem_id);
                mysql_query(conn, q.c_str());
            }
            std::string q = "DELETE FROM users WHERE username ='" + admin_username + "'";
            mysql_query(conn, q.c_str());
            ConnectionPool::instance().release(conn);
        }
        ConnectionPool::instance().close_all();
    }

    httplib::Request make_admin_req() {
        httplib::Request req;
        req.headers.emplace("Cookie", "session_id=" + admin_session);
        return req;
    }
};

TEST_F(AdminApiTest, GetTestCasesEmpty) {
    httplib::Request req = make_admin_req();
    req.path_params["id"] = std::to_string(test_problem_id);
    httplib::Response res;

    handle_get_testcases(req, res);

    EXPECT_EQ(res.status, 200);
    auto j = json::parse(res.body);
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 0);
}

TEST_F(AdminApiTest, AddTestCase) {
    httplib::Request req = make_admin_req();
    req.path_params["id"] = std::to_string(test_problem_id);

    json body;
    body["input"] = "1 2";
    body["expected"] = "3";
    body["is_sample"] = true;
    body["sort_order"] = 0;
    req.body = body.dump();

    httplib::Response res;
    handle_add_testcase(req, res);

    EXPECT_EQ(res.status, 201);
    auto j = json::parse(res.body);
    EXPECT_GT(j["id"], 0);
    EXPECT_EQ(j["message"], "Test case added");
}

TEST_F(AdminApiTest, AddHiddenTestCase) {
    httplib::Request req = make_admin_req();
    req.path_params["id"] = std::to_string(test_problem_id);

    json body;
    body["input"] = "5 7";
    body["expected"] = "12";
    body["is_sample"] = false;
    body["sort_order"] = 1;
    req.body = body.dump();

    httplib::Response res;
    handle_add_testcase(req, res);

    EXPECT_EQ(res.status, 201);
    auto j = json::parse(res.body);
    EXPECT_GT(j["id"], 0);
}

TEST_F(AdminApiTest, GetTestCasesAfterAdd) {
    httplib::Request req = make_admin_req();
    req.path_params["id"] = std::to_string(test_problem_id);

    json body;
    body["input"] = "1 2";
    body["expected"] = "3";
    body["is_sample"] = true;
    body["sort_order"] = 0;
    req.body = body.dump();
    httplib::Response res;
    handle_add_testcase(req, res);
    ASSERT_EQ(res.status, 201);

    body["input"] = "4 5";
    body["expected"] = "9";
    body["is_sample"] = false;
    body["sort_order"] = 1;
    req.body = body.dump();
    handle_add_testcase(req, res);
    ASSERT_EQ(res.status, 201);

    httplib::Request req2 = make_admin_req();
    req2.path_params["id"] = std::to_string(test_problem_id);
    httplib::Response res2;
    handle_get_testcases(req2, res2);

    EXPECT_EQ(res2.status, 200);
    auto arr = json::parse(res2.body);
    EXPECT_EQ(arr.size(), 2);
}

TEST_F(AdminApiTest, DeleteTestCase) {
    httplib::Request req = make_admin_req();
    req.path_params["id"] = std::to_string(test_problem_id);

    json body;
    body["input"] = "1 2";
    body["expected"] = "3";
    body["is_sample"] = true;
    req.body = body.dump();
    httplib::Response res;
    handle_add_testcase(req, res);
    ASSERT_EQ(res.status, 201);
    int tc_id = json::parse(res.body)["id"];

    httplib::Request req2 = make_admin_req();
    req2.path_params["tc_id"] = std::to_string(tc_id);
    httplib::Response res2;
    handle_delete_testcase(req2, res2);

    EXPECT_EQ(res2.status, 200);
    auto j = json::parse(res2.body);
    EXPECT_EQ(j["message"], "Test case deleted");

    httplib::Request req3 = make_admin_req();
    req3.path_params["id"] = std::to_string(test_problem_id);
    httplib::Response res3;
    handle_get_testcases(req3, res3);

    auto arr = json::parse(res3.body);
    EXPECT_EQ(arr.size(), 0);
}

TEST_F(AdminApiTest, GetTestCasesNonAdmin) {
    httplib::Request req;
    req.path_params["id"] = std::to_string(test_problem_id);
    httplib::Response res;

    handle_get_testcases(req, res);
    EXPECT_EQ(res.status, 403);
}

TEST_F(AdminApiTest, AddTestCaseNonAdmin) {
    httplib::Request req;
    req.path_params["id"] = std::to_string(test_problem_id);

    json body;
    body["input"] = "1 2";
    body["expected"] = "3";
    body["is_sample"] = true;
    req.body = body.dump();

    httplib::Response res;
    handle_add_testcase(req, res);
    EXPECT_EQ(res.status, 403);
}

TEST_F(AdminApiTest, DeleteTestCaseNonAdmin) {
    httplib::Request req;
    req.path_params["tc_id"] = "1";
    httplib::Response res;

    handle_delete_testcase(req, res);
    EXPECT_EQ(res.status, 403);
}
