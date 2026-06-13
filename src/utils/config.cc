#include "config.hpp"
#include <fstream>
#include <iostream>

Config& Config::instance() {
    static Config inst;
    return inst;
}

bool Config::load(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) {
        std::cerr << "Failed to open config file: " << filepath << std::endl;
        return false;
    }

    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse config: " << e.what() << std::endl;
        return false;
    }

    auto& srv = j["server"];
    server_.port = srv.value("port", 8080);
    server_.static_dir = srv.value("static_dir", "public");

    auto& db = j["database"];
    database_.host = db.value("host", "localhost");
    database_.port = db.value("port", 3306);
    database_.user = db.value("user", "root");
    database_.password = db.value("password", "");
    database_.dbname = db.value("dbname", "oj_system");
    database_.pool_size = db.value("pool_size", 4);

    auto& sess = j["session"];
    session_.dir = sess.value("dir", "/var/oj/sessions");
    session_.max_files = sess.value("max_files", 10000);
    session_.cleanup_interval_min = sess.value("cleanup_interval_min", 30);

    auto& jdg = j["judge"];
    judge_.time_limit_default = jdg.value("time_limit_default", 2);
    judge_.memory_limit_default = jdg.value("memory_limit_default", 256);
    judge_.max_queue = jdg.value("max_queue", 5);
    judge_.worker_threads = jdg.value("worker_threads", 2);
    judge_.compile_timeout = jdg.value("compile_timeout", 10);
    judge_.output_limit = jdg.value("output_limit", 65536);
    judge_.sandbox_tmp_dir = jdg.value("sandbox_tmp_dir", "/tmp/oj_sandbox");

    auto& rl = j["rate_limit"];
    rate_limit_.submit_window_sec = rl.value("submit_window_sec", 10);
    rate_limit_.max_code_size = rl.value("max_code_size", 65536);

    return true;
}

const ServerConfig& Config::server() const { return server_; }
const DBConfig& Config::database() const { return database_; }
const SessionConfig& Config::session() const { return session_; }
const JudgeConfig& Config::judge() const { return judge_; }
const RateLimitConfig& Config::rate_limit() const { return rate_limit_; }
