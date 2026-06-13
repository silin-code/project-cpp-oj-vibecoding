#include "connection_pool.hpp"
#include "../utils/config.hpp"
#include <iostream>

ConnectionPool& ConnectionPool::instance() {
    static ConnectionPool inst;
    return inst;
}

ConnectionPool::~ConnectionPool() {
    close_all();
}

bool ConnectionPool::init(const DBConfig& config) {
    host_ = config.host;
    port_ = config.port;
    user_ = config.user;
    password_ = config.password;
    dbname_ = config.dbname;
    pool_size_ = config.pool_size;

    if (mysql_library_init(0, nullptr, nullptr) != 0) {
        std::cerr << "mysql_library_init failed" << std::endl;
        return false;
    }

    for (int i = 0; i < pool_size_; ++i) {
        auto* conn = create_connection();
        if (!conn) return false;
        pool_.push_back(conn);
    }

    return true;
}

MYSQL* ConnectionPool::get() {
    std::unique_lock<std::mutex> lock(mtx_);
    while (pool_.empty()) {
        cv_.wait(lock);
    }
    auto* conn = pool_.back();
    pool_.pop_back();

    if (mysql_ping(conn) != 0) {
        mysql_close(conn);
        conn = create_connection();
    }

    return conn;
}

void ConnectionPool::release(MYSQL* conn) {
    std::lock_guard<std::mutex> lock(mtx_);
    pool_.push_back(conn);
    cv_.notify_one();
}

void ConnectionPool::close_all() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto* conn : pool_) {
        mysql_close(conn);
    }
    pool_.clear();
    mysql_library_end();
}

MYSQL* ConnectionPool::create_connection() {
    auto* conn = mysql_init(nullptr);
    if (!conn) return nullptr;

    if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(),
                            password_.c_str(), dbname_.c_str(),
                            port_, nullptr, 0)) {
        std::cerr << "MySQL connect error: " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return nullptr;
    }

    mysql_set_character_set(conn, "utf8mb4");
    return conn;
}
