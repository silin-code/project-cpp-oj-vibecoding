#pragma once

#include <mysql/mysql.h>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>

struct DBConfig;

class ConnectionPool {
public:
    static ConnectionPool& instance();

    bool init(const DBConfig& config);
    MYSQL* get();
    MYSQL* try_get(int timeout_ms = 5000);
    void release(MYSQL* conn);
    void close_all();

private:
    ConnectionPool() = default;
    ~ConnectionPool();
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    MYSQL* create_connection();

    std::vector<MYSQL*> pool_;
    std::mutex mtx_;
    std::condition_variable cv_;
    int pool_size_ = 4;
    std::string host_;
    int port_;
    std::string user_;
    std::string password_;
    std::string dbname_;
};
