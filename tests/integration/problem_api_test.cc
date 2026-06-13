#include <gtest/gtest.h>
#include <cstdio>
#include <ctime>
#include <string>

#include <httplib.h>
#include <json.hpp>

#include "handler/problem_handler.hpp"
#include "handler/auth_handler.hpp"
#include "service/problem_service.hpp"
#include "service/auth_service.hpp"
#include "utils/config.hpp"
#include "utils/logger.hpp"
#include "db/connection_pool.hpp"

using json = nlohmann::json;

class ProblemApiTest : public ::testing::Test {
protected:
    std::string test_username;
    std::string test_password = "test_pass_123";
    std::string admin_username;
    std::string admin_password = "admin_pass_123";
    std::string admin_session;
    int test_problem_id = -1;

    void SetUp() override {
        auto ts = std::to_string(std::time(nullptr));
        test_username = "probapi_user_" + ts;
        admin_username = "probapi_admin_" + ts;

        Config::instance().load("config/config.json");
        Logger::instance().init("/tmp/oj_test_problem_api.log", LogLevel::DEBUG);
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
    }

    void TearDown() override {
        AuthService::instance().shutdown();

        auto* conn = ConnectionPool::instance().get();
        if (conn) {
            std::string q = "DELETE FROM users WHERE username ='" + test_username + "'";
            mysql_query(conn, q.c_str());
            q = "DELETE FROM users WHERE username ='" + admin_username + "'";
            mysql_query(conn, q.c_str());
            if (test_problem_id > 0) {
                q = "DELETE FROM problems WHERE id = " + std::to_string(test_problem_id);
                mysql_query(conn, q.c_str());
            }
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

TEST_F(ProblemApiTest, GetProblemListEmpty) {
    httplib::Request req;
    httplib::Response res;

    handle_get_problems(req, res);

    EXPECT_EQ(res.status, 200);
    auto j = json::parse(res.body);
    EXPECT_TRUE(j.is_array());
}

TEST_F(ProblemApiTest, CreateProblemAsAdmin) {
    httplib::Request req = make_admin_req();
    httplib::Response res;

    json body;
    body["title"] = "Test Problem";
    body["description"] = "Test description";
    body["input_desc"] = "Input desc";
    body["output_desc"] = "Output desc";
    body["difficulty"] = "medium";
    body["time_limit"] = 3;
    body["memory_limit"] = 512;
    req.body = body.dump();

    handle_create_problem(req, res);

    EXPECT_EQ(res.status, 201);
    auto j = json::parse(res.body);
    EXPECT_GT(j["id"], 0);
    EXPECT_EQ(j["message"], "Problem created");
    test_problem_id = j["id"];
}

TEST_F(ProblemApiTest, CreateProblemAsNonAdmin) {
    httplib::Request req;
    httplib::Response res;

    json body;
    body["title"] = "Test Problem";
    body["description"] = "Test desc";
    req.body = body.dump();

    handle_create_problem(req, res);
    EXPECT_EQ(res.status, 403);
}

TEST_F(ProblemApiTest, GetProblemDetail) {
    httplib::Request req = make_admin_req();
    httplib::Response res;

    json body;
    body["title"] = "Detail Test";
    body["description"] = "A detailed problem";
    body["input_desc"] = "Two integers";
    body["output_desc"] = "Sum";
    body["difficulty"] = "hard";
    body["time_limit"] = 5;
    body["memory_limit"] = 1024;
    req.body = body.dump();
    handle_create_problem(req, res);
    ASSERT_EQ(res.status, 201);
    test_problem_id = json::parse(res.body)["id"];

    httplib::Request req2;
    req2.path_params["id"] = std::to_string(test_problem_id);

    httplib::Response res2;
    handle_get_problem(req2, res2);

    EXPECT_EQ(res2.status, 200);
    auto j = json::parse(res2.body);
    EXPECT_EQ(j["title"], "Detail Test");
    EXPECT_EQ(j["description"], "A detailed problem");
    EXPECT_EQ(j["difficulty"], "hard");
    EXPECT_EQ(j["time_limit"], 5);
    EXPECT_EQ(j["memory_limit"], 1024);
    EXPECT_TRUE(j.contains("sample_cases"));
}

TEST_F(ProblemApiTest, GetProblemDetailNotFound) {
    httplib::Request req;
    req.path_params["id"] = "999999";
    httplib::Response res;

    handle_get_problem(req, res);

    EXPECT_EQ(res.status, 404);
    auto j = json::parse(res.body);
    EXPECT_EQ(j["error"], "Problem not found");
}

TEST_F(ProblemApiTest, GetProblemListWithData) {
    httplib::Request req = make_admin_req();
    httplib::Response res;

    json body;
    body["title"] = "List Test";
    body["description"] = "desc";
    req.body = body.dump();
    handle_create_problem(req, res);
    ASSERT_EQ(res.status, 201);
    test_problem_id = json::parse(res.body)["id"];

    httplib::Request req2;
    httplib::Response res2;
    handle_get_problems(req2, res2);

    EXPECT_EQ(res2.status, 200);
    auto arr = json::parse(res2.body);
    EXPECT_TRUE(arr.is_array());
    bool found = false;
    for (auto& item : arr) {
        if (item["id"] == test_problem_id) {
            found = true;
            EXPECT_EQ(item["title"], "List Test");
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ProblemApiTest, UpdateProblemAsAdmin) {
    httplib::Request req = make_admin_req();
    httplib::Response res;

    json body;
    body["title"] = "Original";
    body["description"] = "Original desc";
    req.body = body.dump();
    handle_create_problem(req, res);
    ASSERT_EQ(res.status, 201);
    test_problem_id = json::parse(res.body)["id"];

    json upd_body;
    upd_body["title"] = "Updated";
    upd_body["description"] = "Updated desc";
    upd_body["difficulty"] = "hard";
    upd_body["time_limit"] = 10;
    upd_body["memory_limit"] = 2048;
    req.body = upd_body.dump();
    req.path_params["id"] = std::to_string(test_problem_id);

    httplib::Response res2;
    handle_update_problem(req, res2);

    EXPECT_EQ(res2.status, 200);
    auto j = json::parse(res2.body);
    EXPECT_EQ(j["message"], "Problem updated");
}

TEST_F(ProblemApiTest, UpdateProblemAsNonAdmin) {
    httplib::Request req = make_admin_req();
    httplib::Response res;

    json body;
    body["title"] = "Original";
    body["description"] = "desc";
    req.body = body.dump();
    handle_create_problem(req, res);
    ASSERT_EQ(res.status, 201);
    test_problem_id = json::parse(res.body)["id"];

    httplib::Request req2;
    req2.path_params["id"] = std::to_string(test_problem_id);
    json upd_body;
    upd_body["title"] = "Hacked";
    req2.body = upd_body.dump();

    httplib::Response res2;
    handle_update_problem(req2, res2);
    EXPECT_EQ(res2.status, 403);
}

TEST_F(ProblemApiTest, DeleteProblemAsAdmin) {
    httplib::Request req = make_admin_req();
    httplib::Response res;

    json body;
    body["title"] = "To Delete";
    body["description"] = "desc";
    req.body = body.dump();
    handle_create_problem(req, res);
    ASSERT_EQ(res.status, 201);
    test_problem_id = json::parse(res.body)["id"];

    req.path_params["id"] = std::to_string(test_problem_id);
    httplib::Response res2;
    handle_delete_problem(req, res2);

    EXPECT_EQ(res2.status, 200);
    auto j = json::parse(res2.body);
    EXPECT_EQ(j["message"], "Problem deleted");

    int deleted_id = test_problem_id;
    test_problem_id = -1;

    EXPECT_FALSE(ProblemService::instance().problem_exists(deleted_id));
}

TEST_F(ProblemApiTest, DeleteProblemAsNonAdmin) {
    httplib::Request req = make_admin_req();
    httplib::Response res;

    json body;
    body["title"] = "No Delete";
    body["description"] = "desc";
    req.body = body.dump();
    handle_create_problem(req, res);
    ASSERT_EQ(res.status, 201);
    test_problem_id = json::parse(res.body)["id"];

    httplib::Request req2;
    req2.path_params["id"] = std::to_string(test_problem_id);

    httplib::Response res2;
    handle_delete_problem(req2, res2);
    EXPECT_EQ(res2.status, 403);
}
