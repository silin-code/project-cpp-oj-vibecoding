#pragma once

#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>

struct MYSQL;

struct SubmitTask {
    int submission_id;
    int problem_id;
    int user_id;
    std::string code;
    int time_limit;
    int memory_limit;
};

class ExecutorService {
public:
    static ExecutorService& instance();

    bool init();
    void shutdown();

    bool enqueue(int submission_id, int problem_id, int user_id,
                 const std::string& code, int time_limit, int memory_limit);

    bool compile(const std::string& src_file, const std::string& bin_file,
                 std::string& error_msg);
    int run_sandbox(const std::string& bin_file, const std::string& input_str,
                    std::string& output, std::string& error_msg,
                    int& time_used_ms, int& mem_used_kb,
                    int time_limit_sec, int mem_limit_mb);
    std::string trim_output(const std::string& s);

private:
    ExecutorService() = default;
    ~ExecutorService();
    ExecutorService(const ExecutorService&) = delete;
    ExecutorService& operator=(const ExecutorService&) = delete;

    void worker_loop();
    void judge(SubmitTask task);
    void update_status(int submission_id, const std::string& status,
                       int failed_case, const std::string& error_msg,
                       int time_used, int memory_used,
                       struct MYSQL* conn = nullptr);

    std::queue<SubmitTask> queue_;
    std::mutex queue_mtx_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
};
