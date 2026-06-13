#include <gtest/gtest.h>
#include <cstdio>
#include <ctime>
#include <string>

#include "service/auth_service.hpp"
#include "utils/config.hpp"
#include "utils/logger.hpp"
#include "db/connection_pool.hpp"

class AuthTest : public ::testing::Test {
protected:
    std::string test_username;
    std::string test_password = "test_pass_123";
    int test_user_id = -1;
    std::string test_session_id;

    void SetUp() override {
        test_username = "testuser_" + std::to_string(std::time(nullptr));

        Config::instance().load("config/config.json");
        Logger::instance().init("/tmp/oj_test_auth.log", LogLevel::DEBUG);
        ConnectionPool::instance().init(Config::instance().database());
        AuthService::instance().init();
    }

    void TearDown() override {
        AuthService::instance().shutdown();

        auto* conn = ConnectionPool::instance().get();
        if (conn) {
            std::string q = "DELETE FROM users WHERE username = '"
                + test_username + "'";
            mysql_query(conn, q.c_str());
            ConnectionPool::instance().release(conn);
        }

        ConnectionPool::instance().close_all();
    }
};

TEST_F(AuthTest, RegisterSuccess) {
    std::string err;
    int uid = AuthService::instance().register_user(test_username, test_password, err);
    EXPECT_GT(uid, 0) << "register_user failed: " << err;
    test_user_id = uid;
}

TEST_F(AuthTest, RegisterDuplicate) {
    std::string err;
    int uid1 = AuthService::instance().register_user(test_username, test_password, err);
    ASSERT_GT(uid1, 0);
    test_user_id = uid1;

    int uid2 = AuthService::instance().register_user(test_username, test_password, err);
    EXPECT_EQ(uid2, -1);
    EXPECT_EQ(err, "Username already exists");
}

TEST_F(AuthTest, RegisterEmptyFields) {
    std::string err;

    int uid = AuthService::instance().register_user("", test_password, err);
    EXPECT_EQ(uid, -1);
    EXPECT_FALSE(err.empty());

    uid = AuthService::instance().register_user(test_username, "", err);
    EXPECT_EQ(uid, -1);
    EXPECT_FALSE(err.empty());
}

TEST_F(AuthTest, LoginSuccess) {
    std::string err;
    test_user_id = AuthService::instance().register_user(test_username, test_password, err);
    ASSERT_GT(test_user_id, 0);

    std::string sid = AuthService::instance().login(test_username, test_password, err);
    EXPECT_FALSE(sid.empty()) << "login failed: " << err;
    test_session_id = sid;
}

TEST_F(AuthTest, LoginWrongPassword) {
    std::string err;
    test_user_id = AuthService::instance().register_user(test_username, test_password, err);
    ASSERT_GT(test_user_id, 0);

    std::string sid = AuthService::instance().login(test_username, "wrong_pass", err);
    EXPECT_TRUE(sid.empty());
    EXPECT_EQ(err, "Invalid username or password");
}

TEST_F(AuthTest, LoginWrongUsername) {
    std::string err;
    std::string sid = AuthService::instance().login("nonexistent_user", test_password, err);
    EXPECT_TRUE(sid.empty());
    EXPECT_EQ(err, "Invalid username or password");
}

TEST_F(AuthTest, AuthenticateValidSession) {
    std::string err;
    test_user_id = AuthService::instance().register_user(test_username, test_password, err);
    ASSERT_GT(test_user_id, 0);
    test_session_id = AuthService::instance().login(test_username, test_password, err);
    ASSERT_FALSE(test_session_id.empty());

    SessionInfo info = AuthService::instance().authenticate(test_session_id);
    EXPECT_EQ(info.user_id, test_user_id);
    EXPECT_EQ(info.username, test_username);
    EXPECT_EQ(info.role, "user");
}

TEST_F(AuthTest, AuthenticateInvalidSession) {
    SessionInfo info = AuthService::instance().authenticate("invalid_session_id_12345");
    EXPECT_EQ(info.user_id, 0);
}

TEST_F(AuthTest, AuthenticateEmptySession) {
    SessionInfo info = AuthService::instance().authenticate("");
    EXPECT_EQ(info.user_id, 0);
}

TEST_F(AuthTest, Logout) {
    std::string err;
    test_user_id = AuthService::instance().register_user(test_username, test_password, err);
    ASSERT_GT(test_user_id, 0);
    test_session_id = AuthService::instance().login(test_username, test_password, err);
    ASSERT_FALSE(test_session_id.empty());

    bool ok = AuthService::instance().logout(test_session_id);
    EXPECT_TRUE(ok);

    SessionInfo info = AuthService::instance().authenticate(test_session_id);
    EXPECT_EQ(info.user_id, 0);
}

TEST_F(AuthTest, LogoutInvalidSession) {
    bool ok = AuthService::instance().logout("nonexistent_session");
    EXPECT_FALSE(ok);
}

TEST_F(AuthTest, RateLimitBlocksRepeatedSubmit) {
    std::string err;
    test_user_id = AuthService::instance().register_user(test_username, test_password, err);
    ASSERT_GT(test_user_id, 0);

    EXPECT_TRUE(AuthService::instance().check_rate_limit(test_user_id));
    EXPECT_FALSE(AuthService::instance().check_rate_limit(test_user_id));
}
