#include <gtest/gtest.h>
#include <cstdio>
#include <ctime>
#include <string>

#include <httplib.h>
#include <json.hpp>

#include "handler/auth_handler.hpp"
#include "service/auth_service.hpp"
#include "utils/config.hpp"
#include "utils/logger.hpp"
#include "db/connection_pool.hpp"

using json = nlohmann::json;

class AuthApiTest : public ::testing::Test {
protected:
    std::string test_username;
    std::string test_password = "test_pass_123";

    void SetUp() override {
        test_username = "apitest_" + std::to_string(std::time(nullptr));

        Config::instance().load("config/config.json");
        Logger::instance().init("/tmp/oj_test_auth_api.log", LogLevel::DEBUG);
        ConnectionPool::instance().init(Config::instance().database());
        AuthService::instance().init();
    }

    void TearDown() override {
        AuthService::instance().shutdown();

        auto* conn = ConnectionPool::instance().get();
        if (conn) {
            std::string q = "DELETE FROM users WHERE username = '" + test_username + "'";
            mysql_query(conn, q.c_str());
            ConnectionPool::instance().release(conn);
        }
        ConnectionPool::instance().close_all();
    }
};

TEST_F(AuthApiTest, RegisterSuccess) {
    httplib::Request req;
    httplib::Response res;
    json body;
    body["username"] = test_username;
    body["password"] = test_password;
    req.body = body.dump();

    handle_register(req, res);

    EXPECT_EQ(res.status, 201);
    auto j = json::parse(res.body);
    EXPECT_EQ(j["username"], test_username);
    EXPECT_GT(j["id"], 0);
}

TEST_F(AuthApiTest, RegisterDuplicateUsername) {
    httplib::Request req;
    httplib::Response res;
    json body;
    body["username"] = test_username;
    body["password"] = test_password;
    req.body = body.dump();

    handle_register(req, res);
    ASSERT_EQ(res.status, 201);

    handle_register(req, res);
    EXPECT_EQ(res.status, 409);
    auto j = json::parse(res.body);
    EXPECT_EQ(j["error"], "Username already exists");
}

TEST_F(AuthApiTest, RegisterMissingFields) {
    httplib::Request req;
    httplib::Response res;

    json body1;
    body1["username"] = "";
    body1["password"] = "pass";
    req.body = body1.dump();
    handle_register(req, res);
    EXPECT_EQ(res.status, 400);

    json body2;
    body2["username"] = "user";
    body2["password"] = "";
    req.body = body2.dump();
    handle_register(req, res);
    EXPECT_EQ(res.status, 400);
}

TEST_F(AuthApiTest, RegisterInvalidJson) {
    httplib::Request req;
    httplib::Response res;
    req.body = "not json at all";

    handle_register(req, res);
    EXPECT_EQ(res.status, 400);
}

TEST_F(AuthApiTest, LoginSuccess) {
    httplib::Request req;
    httplib::Response res;

    json reg_body;
    reg_body["username"] = test_username;
    reg_body["password"] = test_password;
    req.body = reg_body.dump();
    handle_register(req, res);
    ASSERT_EQ(res.status, 201);

    req.body = reg_body.dump();
    handle_login(req, res);

    EXPECT_EQ(res.status, 200);
    auto j = json::parse(res.body);
    EXPECT_EQ(j["message"], "Login successful");
    EXPECT_EQ(j["username"], test_username);

    bool has_cookie = false;
    for (auto& [k, v] : res.headers) {
        if (k == "Set-Cookie" && v.find("session_id=") != std::string::npos) {
            has_cookie = true;
            break;
        }
    }
    EXPECT_TRUE(has_cookie);
}

TEST_F(AuthApiTest, LoginWrongPassword) {
    httplib::Request req;
    httplib::Response res;

    json reg_body;
    reg_body["username"] = test_username;
    reg_body["password"] = test_password;
    req.body = reg_body.dump();
    handle_register(req, res);
    ASSERT_EQ(res.status, 201);

    json bad_body;
    bad_body["username"] = test_username;
    bad_body["password"] = "wrong";
    req.body = bad_body.dump();
    handle_login(req, res);

    EXPECT_EQ(res.status, 401);
}

TEST_F(AuthApiTest, LoginMissingFields) {
    httplib::Request req;
    httplib::Response res;

    json body1;
    body1["username"] = "";
    body1["password"] = "pass";
    req.body = body1.dump();
    handle_login(req, res);
    EXPECT_EQ(res.status, 400);

    json body2;
    body2["username"] = "user";
    body2["password"] = "";
    req.body = body2.dump();
    handle_login(req, res);
    EXPECT_EQ(res.status, 400);
}

TEST_F(AuthApiTest, GetMeAuthenticated) {
    httplib::Request req;
    httplib::Response res;

    json reg_body;
    reg_body["username"] = test_username;
    reg_body["password"] = test_password;
    req.body = reg_body.dump();
    handle_register(req, res);
    ASSERT_EQ(res.status, 201);

    req.body = reg_body.dump();
    handle_login(req, res);
    ASSERT_EQ(res.status, 200);

    std::string session_cookie;
    for (auto& [k, v] : res.headers) {
        if (k == "Set-Cookie") {
            session_cookie = v;
            break;
        }
    }
    req.headers.emplace("Cookie", session_cookie);

    httplib::Response res2;
    handle_me(req, res2);

    EXPECT_EQ(res2.status, 200);
    auto j = json::parse(res2.body);
    EXPECT_EQ(j["username"], test_username);
    EXPECT_EQ(j["role"], "user");
}

TEST_F(AuthApiTest, GetMeUnauthenticated) {
    httplib::Request req;
    httplib::Response res;

    handle_me(req, res);
    EXPECT_EQ(res.status, 401);
}

TEST_F(AuthApiTest, Logout) {
    httplib::Request req;
    httplib::Response res;

    json reg_body;
    reg_body["username"] = test_username;
    reg_body["password"] = test_password;
    req.body = reg_body.dump();
    handle_register(req, res);
    ASSERT_EQ(res.status, 201);

    req.body = reg_body.dump();
    handle_login(req, res);
    ASSERT_EQ(res.status, 200);

    std::string session_cookie;
    for (auto& [k, v] : res.headers) {
        if (k == "Set-Cookie") {
            session_cookie = v;
            break;
        }
    }
    req.headers.emplace("Cookie", session_cookie);

    httplib::Response res2;
    handle_logout(req, res2);

    EXPECT_EQ(res2.status, 200);
    auto j = json::parse(res2.body);
    EXPECT_EQ(j["message"], "Logged out");
}

TEST_F(AuthApiTest, GetMeAfterLogout) {
    httplib::Request req;
    httplib::Response res;

    json reg_body;
    reg_body["username"] = test_username;
    reg_body["password"] = test_password;
    req.body = reg_body.dump();
    handle_register(req, res);
    ASSERT_EQ(res.status, 201);

    req.body = reg_body.dump();
    handle_login(req, res);
    ASSERT_EQ(res.status, 200);

    std::string session_cookie;
    for (auto& [k, v] : res.headers) {
        if (k == "Set-Cookie") {
            session_cookie = v;
            break;
        }
    }
    req.headers.emplace("Cookie", session_cookie);

    handle_logout(req, res);

    httplib::Response res2;
    handle_me(req, res2);
    EXPECT_EQ(res2.status, 401);
}

TEST_F(AuthApiTest, GetMeInvalidSession) {
    httplib::Request req;
    httplib::Response res;

    req.headers.insert({"Cookie", "session_id=invalid_session_id_12345"});

    handle_me(req, res);
    EXPECT_EQ(res.status, 401);
}
