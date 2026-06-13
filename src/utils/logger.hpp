#pragma once

#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static Logger& instance();

    void init(const std::string& filepath, LogLevel level = LogLevel::INFO);
    void set_level(LogLevel level);

    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(LogLevel level, const std::string& msg);
    std::string level_str(LogLevel level);
    std::string timestamp();

    std::ofstream file_;
    std::mutex mtx_;
    LogLevel level_ = LogLevel::INFO;
};
