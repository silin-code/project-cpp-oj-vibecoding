#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>

struct SessionInfo {
    int user_id = 0;
    std::string username;
    std::string role;
};

class AuthService {
public:
    static AuthService& instance();

    bool init();
    void shutdown();

    int register_user(const std::string& username, const std::string& password, std::string& error_msg);
    std::string login(const std::string& username, const std::string& password, std::string& error_msg);
    bool logout(const std::string& session_id);
    SessionInfo authenticate(const std::string& session_id);
    bool check_rate_limit(int user_id);

private:
    AuthService() = default;
    ~AuthService();
    AuthService(const AuthService&) = delete;
    AuthService& operator=(const AuthService&) = delete;

    std::string generate_session_id();
    std::string make_salt();
    std::string hash_password(const std::string& password);
    bool verify_password(const std::string& password, const std::string& stored_hash);
    std::string session_path(const std::string& session_id);
    void cleanup_expired();
    void cleanup_loop();

    std::thread cleanup_thread_;
    bool stop_cleanup_ = false;
    std::mutex cleanup_mutex_;
    std::condition_variable cleanup_cv_;
    std::mutex rate_limit_mutex_;

    std::unordered_map<int, std::chrono::steady_clock::time_point> last_submit_;
    std::string session_dir_;
    int submit_window_sec_ = 10;
    int cleanup_interval_min_ = 30;
};
