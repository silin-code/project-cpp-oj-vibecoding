#include "logger.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(const std::string& filepath, LogLevel level) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (file_.is_open()) file_.close();
    file_.open(filepath, std::ios::app);
    level_ = level;
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mtx_);
    level_ = level;
}

Logger::~Logger() {
    if (file_.is_open()) file_.close();
}

void Logger::debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
void Logger::info(const std::string& msg)  { log(LogLevel::INFO, msg); }
void Logger::warn(const std::string& msg)  { log(LogLevel::WARN, msg); }
void Logger::error(const std::string& msg) { log(LogLevel::ERROR, msg); }

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < level_) return;
    std::lock_guard<std::mutex> lock(mtx_);
    if (!file_.is_open()) return;
    file_ << timestamp() << " [" << level_str(level) << "] " << msg << std::endl;
}

std::string Logger::level_str(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

std::string Logger::timestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}
