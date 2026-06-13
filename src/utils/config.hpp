#pragma once

#include <string>
#include <json.hpp>

using json = nlohmann::json;

struct DBConfig {
    std::string host;
    int port;
    std::string user;
    std::string password;
    std::string dbname;
    int pool_size;
};

struct ServerConfig {
    int port;
    std::string static_dir;
};

struct SessionConfig {
    std::string dir;
    int max_files;
    int cleanup_interval_min;
};

struct JudgeConfig {
    int time_limit_default;
    int memory_limit_default;
    int max_queue;
    int worker_threads;
    int compile_timeout;
    int output_limit;
    std::string sandbox_tmp_dir;
};

struct RateLimitConfig {
    int submit_window_sec;
    int max_code_size;
};

class Config {
public:
    static Config& instance();

    bool load(const std::string& filepath);

    const ServerConfig& server() const;
    const DBConfig& database() const;
    const SessionConfig& session() const;
    const JudgeConfig& judge() const;
    const RateLimitConfig& rate_limit() const;

private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    ServerConfig server_;
    DBConfig database_;
    SessionConfig session_;
    JudgeConfig judge_;
    RateLimitConfig rate_limit_;
};
