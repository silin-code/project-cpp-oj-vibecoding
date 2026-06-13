#include "auth_service.hpp"
#include "../db/connection_pool.hpp"
#include "../utils/config.hpp"
#include "../utils/logger.hpp"
#include <json.hpp>
#include <crypt.h>
#include <openssl/sha.h>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <vector>

AuthService& AuthService::instance() {
    static AuthService inst;
    return inst;
}

AuthService::~AuthService() {
    shutdown();
}

bool AuthService::init() {
    auto& cfg = Config::instance();
    session_dir_ = cfg.session().dir;
    submit_window_sec_ = cfg.rate_limit().submit_window_sec;
    cleanup_interval_min_ = cfg.session().cleanup_interval_min;

    {
        bool ok = true;
        size_t pos = 0;
        while ((pos = session_dir_.find('/', pos + 1)) != std::string::npos) {
            std::string sub = session_dir_.substr(0, pos);
            if (mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) {
                ok = false;
                break;
            }
        }
        if (ok) {
            ok = (mkdir(session_dir_.c_str(), 0755) == 0 || errno == EEXIST);
        }
        if (!ok) {
            session_dir_ = "/tmp/oj_sessions";
            mkdir(session_dir_.c_str(), 0755);
            Logger::instance().warn("Session dir fallback to " + session_dir_);
        }
    }

    stop_cleanup_ = false;
    cleanup_thread_ = std::thread(&AuthService::cleanup_loop, this);

    Logger::instance().info("AuthService initialized, session dir: " + session_dir_);
    return true;
}

void AuthService::shutdown() {
    {
        std::lock_guard<std::mutex> lock(cleanup_mutex_);
        stop_cleanup_ = true;
    }
    cleanup_cv_.notify_one();
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

static std::string to_hex(const unsigned char* data, size_t len) {
    static const char hex[] = "0123456789abcdef";
    std::string result(len * 2, '\0');
    for (size_t i = 0; i < len; i++) {
        result[i * 2] = hex[data[i] >> 4];
        result[i * 2 + 1] = hex[data[i] & 0x0f];
    }
    return result;
}

static std::string sha256_hash(const std::string& password) {
    unsigned char salt[16];
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) return "";
    fread(salt, 1, 16, f);
    fclose(f);

    std::string to_hash(reinterpret_cast<const char*>(salt), 16);
    to_hash += password;

    unsigned char hash[32];
    SHA256(reinterpret_cast<const unsigned char*>(to_hash.data()), to_hash.size(), hash);

    return "sha256$" + to_hex(salt, 16) + "$" + to_hex(hash, 32);
}

static bool verify_sha256(const std::string& password, const std::string& stored) {
    if (stored.substr(0, 7) != "sha256$") return false;
    auto salt_end = stored.find('$', 7);
    if (salt_end == std::string::npos) return false;
    std::string salt_hex = stored.substr(7, salt_end - 7);
    if (salt_hex.size() != 32) return false;
    std::string expected_hash_hex = stored.substr(salt_end + 1);
    if (expected_hash_hex.size() != 64) return false;

    unsigned char salt[16];
    for (int i = 0; i < 16; i++) {
        salt[i] = std::stoi(salt_hex.substr(i * 2, 2), nullptr, 16);
    }

    std::string to_hash(reinterpret_cast<const char*>(salt), 16);
    to_hash += password;

    unsigned char hash[32];
    SHA256(reinterpret_cast<const unsigned char*>(to_hash.data()), to_hash.size(), hash);

    return expected_hash_hex == to_hex(hash, 32);
}

std::string AuthService::make_salt() {
    static const char b64[] =
        "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    unsigned char random[16];
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) return "";
    fread(random, 1, 16, f);
    fclose(f);

    char salt[30];
    std::memcpy(salt, "$2b$12$", 7);

    int i = 0, j = 7;
    while (i < 16) {
        int c1 = random[i++] & 0xff;
        salt[j++] = b64[c1 >> 2];
        if (i >= 16) {
            salt[j++] = b64[(c1 & 0x03) << 4];
            break;
        }
        int c2 = random[i++] & 0xff;
        salt[j++] = b64[((c1 & 0x03) << 4) | (c2 >> 4)];
        if (i >= 16) {
            salt[j++] = b64[(c2 & 0x0f) << 2];
            break;
        }
        int c3 = random[i++] & 0xff;
        salt[j++] = b64[((c2 & 0x0f) << 2) | (c3 >> 6)];
        salt[j++] = b64[c3 & 0x3f];
    }
    salt[j] = '\0';
    return std::string(salt);
}

std::string AuthService::hash_password(const std::string& password) {
    std::string salt = make_salt();
    if (salt.empty()) {
        Logger::instance().error("Failed to generate bcrypt salt");
        return sha256_hash(password);
    }

    struct crypt_data data;
    data.initialized = 0;
    char* result = crypt_r(password.c_str(), salt.c_str(), &data);
    if (result && result[0] == '$' && result[1] == '2') {
        return std::string(result);
    }

    Logger::instance().warn("bcrypt not available via crypt_r, falling back to SHA-256");
    return sha256_hash(password);
}

bool AuthService::verify_password(const std::string& password, const std::string& stored_hash) {
    if (stored_hash.substr(0, 7) == "sha256$") {
        return verify_sha256(password, stored_hash);
    }

    struct crypt_data data;
    data.initialized = 0;
    char* result = crypt_r(password.c_str(), stored_hash.c_str(), &data);
    if (!result) return false;
    return stored_hash == result;
}

std::string AuthService::generate_session_id() {
    unsigned char random[32];
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) return "";
    fread(random, 1, 32, f);
    fclose(f);
    return to_hex(random, 32);
}

std::string AuthService::session_path(const std::string& session_id) {
    return session_dir_ + "/" + session_id + ".json";
}

int AuthService::register_user(const std::string& username, const std::string& password, std::string& error_msg) {
    if (username.empty() || password.empty()) {
        error_msg = "Username and password are required";
        return -1;
    }

    auto* conn = ConnectionPool::instance().get();
    if (!conn) {
        error_msg = "Database connection failed";
        return -1;
    }

    std::vector<char> esc_buf(username.size() * 2 + 1);
    unsigned long esc_len = mysql_real_escape_string(conn, esc_buf.data(), username.c_str(), username.size());
    std::string esc_username(esc_buf.data(), esc_len);

    std::string password_hash = hash_password(password);
    if (password_hash.empty()) {
        error_msg = "Failed to hash password";
        ConnectionPool::instance().release(conn);
        return -1;
    }

    std::vector<char> hash_buf(password_hash.size() * 2 + 1);
    unsigned long hash_len = mysql_real_escape_string(conn, hash_buf.data(), password_hash.c_str(), password_hash.size());
    std::string esc_hash(hash_buf.data(), hash_len);

    std::string query = "INSERT INTO users (username, password, role) VALUES ('"
        + esc_username + "', '" + esc_hash + "', 'user')";

    if (mysql_query(conn, query.c_str()) != 0) {
        if (mysql_errno(conn) == 1062) {
            error_msg = "Username already exists";
        } else {
            error_msg = "Database error";
            Logger::instance().error(std::string(mysql_error(conn)));
        }
        ConnectionPool::instance().release(conn);
        return -1;
    }

    int user_id = mysql_insert_id(conn);
    ConnectionPool::instance().release(conn);

    Logger::instance().info("User registered: " + username + " (id=" + std::to_string(user_id) + ")");
    return user_id;
}

std::string AuthService::login(const std::string& username, const std::string& password, std::string& error_msg) {
    auto* conn = ConnectionPool::instance().get();
    if (!conn) {
        error_msg = "Database connection failed";
        return "";
    }

    std::vector<char> esc_buf(username.size() * 2 + 1);
    unsigned long esc_len = mysql_real_escape_string(conn, esc_buf.data(), username.c_str(), username.size());
    std::string esc_username(esc_buf.data(), esc_len);

    std::string query = "SELECT id, password, role FROM users WHERE username = '" + esc_username + "'";
    if (mysql_query(conn, query.c_str()) != 0) {
        error_msg = "Database query failed";
        Logger::instance().error(std::string(mysql_error(conn)));
        ConnectionPool::instance().release(conn);
        return "";
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0) {
        if (result) mysql_free_result(result);
        error_msg = "Invalid username or password";
        ConnectionPool::instance().release(conn);
        return "";
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    int user_id = std::stoi(row[0]);
    std::string password_hash = row[1] ? row[1] : "";
    std::string role = row[2] ? row[2] : "user";
    mysql_free_result(result);
    ConnectionPool::instance().release(conn);

    if (password_hash.empty() || !verify_password(password, password_hash)) {
        error_msg = "Invalid username or password";
        return "";
    }

    std::string session_id = generate_session_id();
    if (session_id.empty()) {
        error_msg = "Failed to generate session ID";
        return "";
    }

    auto now = std::chrono::system_clock::now();
    auto expires = now + std::chrono::hours(24);
    auto expires_ts = std::chrono::system_clock::to_time_t(expires);

    nlohmann::json j;
    j["user_id"] = user_id;
    j["username"] = username;
    j["role"] = role;
    j["expires_at"] = expires_ts;

    std::string filepath = session_path(session_id);
    std::ofstream file(filepath);
    if (!file.is_open()) {
        error_msg = "Failed to create session file";
        Logger::instance().error("Cannot write session file: " + filepath);
        return "";
    }
    file << j.dump();
    file.close();

    return session_id;
}

bool AuthService::logout(const std::string& session_id) {
    if (session_id.empty()) return false;
    std::string filepath = session_path(session_id);
    if (unlink(filepath.c_str()) == 0) {
        Logger::instance().info("Session deleted: " + session_id);
        return true;
    }
    return false;
}

SessionInfo AuthService::authenticate(const std::string& session_id) {
    SessionInfo info;
    if (session_id.empty()) return info;

    std::string filepath = session_path(session_id);
    std::ifstream file(filepath);
    if (!file.is_open()) return info;

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        return info;
    }

    auto expires_at = j.value("expires_at", 0LL);
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    if (expires_at < now) {
        unlink(filepath.c_str());
        return info;
    }

    info.user_id = j.value("user_id", 0);
    info.username = j.value("username", "");
    info.role = j.value("role", "user");
    return info;
}

bool AuthService::check_rate_limit(int user_id) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);

    auto it = last_submit_.find(user_id);
    if (it != last_submit_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
        if (elapsed < submit_window_sec_) {
            return false;
        }
    }

    last_submit_[user_id] = now;
    return true;
}

void AuthService::cleanup_expired() {
    DIR* dir = opendir(session_dir_.c_str());
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() <= 5 || name.substr(name.size() - 5) != ".json") continue;

        std::string filepath = session_dir_ + "/" + name;

        std::ifstream file(filepath);
        if (!file.is_open()) continue;

        nlohmann::json j;
        try {
            file >> j;
        } catch (...) {
            file.close();
            unlink(filepath.c_str());
            continue;
        }
        file.close();

        auto expires_at = j.value("expires_at", 0LL);
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        if (expires_at < now) {
            unlink(filepath.c_str());
            Logger::instance().debug("Cleaned up expired session: " + name);
        }
    }
    closedir(dir);
}

void AuthService::cleanup_loop() {
    cleanup_expired();

    std::unique_lock<std::mutex> lock(cleanup_mutex_);
    while (!stop_cleanup_) {
        cleanup_cv_.wait_for(lock, std::chrono::minutes(cleanup_interval_min_));
        if (stop_cleanup_) break;
        lock.unlock();
        cleanup_expired();
        lock.lock();
    }
}
