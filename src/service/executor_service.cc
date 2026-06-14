#include "executor_service.hpp"
#include "../db/connection_pool.hpp"
#include "../utils/config.hpp"
#include "../utils/logger.hpp"

#include <seccomp.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <chrono>
#include <mysql/mysql.h>

namespace fs = std::filesystem;

ExecutorService& ExecutorService::instance() {
    static ExecutorService inst;
    return inst;
}

ExecutorService::~ExecutorService() {
    shutdown();
}

bool ExecutorService::init() {
    running_ = true;
    int n = Config::instance().judge().worker_threads;
    if (n < 1) n = 1;

    fs::create_directories(Config::instance().judge().sandbox_tmp_dir);

    for (int i = 0; i < n; ++i) {
        workers_.emplace_back(&ExecutorService::worker_loop, this);
    }

    Logger::instance().info("ExecutorService initialized with " + std::to_string(n) + " workers");
    return true;
}

void ExecutorService::shutdown() {
    running_ = false;
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

bool ExecutorService::enqueue(int submission_id, int problem_id, int user_id,
                               const std::string& code, int time_limit, int memory_limit) {
    SubmitTask task{submission_id, problem_id, user_id, code, time_limit, memory_limit};
    {
        std::lock_guard<std::mutex> lock(queue_mtx_);
        if (queue_.size() >= (size_t)Config::instance().judge().max_queue) {
            return false;
        }
        queue_.push(std::move(task));
    }
    cv_.notify_one();
    return true;
}

void ExecutorService::worker_loop() {
    while (running_) {
        SubmitTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mtx_);
            cv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return !running_ || !queue_.empty();
            });
            if (!running_ || queue_.empty()) continue;
            task = std::move(queue_.front());
            queue_.pop();
        }
        update_status(task.submission_id, "JUDGING", 0, "", 0, 0);
        judge(task);
    }
}

void ExecutorService::judge(SubmitTask task) {
    auto sandbox_dir = fs::path(Config::instance().judge().sandbox_tmp_dir)
                       / std::to_string(task.submission_id);
    fs::create_directories(sandbox_dir);

    auto src_file = sandbox_dir / "source.cpp";
    auto bin_file = sandbox_dir / "prog";

    {
        std::ofstream f(src_file);
        if (!f.is_open()) {
            update_status(task.submission_id, "RE", 0, "Failed to write source file", 0, 0);
            fs::remove_all(sandbox_dir);
            return;
        }
        f << task.code;
    }

    std::string compile_err;
    if (!compile(src_file.string(), bin_file.string(), compile_err)) {
        update_status(task.submission_id, "CE", 0, compile_err, 0, 0);
        fs::remove_all(sandbox_dir);
        return;
    }

    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) {
        update_status(task.submission_id, "RE", 0, "Internal error", 0, 0);
        fs::remove_all(sandbox_dir);
        return;
    }

    std::string q = "SELECT id, input, expected FROM testcases "
                    "WHERE problem_id=" + std::to_string(task.problem_id)
                    + " ORDER BY sort_order, id";

    if (mysql_query(conn, q.c_str()) != 0) {
        pool.release(conn);
        update_status(task.submission_id, "RE", 0, "DB query failed", 0, 0);
        fs::remove_all(sandbox_dir);
        return;
    }

    auto* result = mysql_store_result(conn);
    if (!result) {
        pool.release(conn);
        update_status(task.submission_id, "RE", 0, "DB query failed", 0, 0);
        fs::remove_all(sandbox_dir);
        return;
    }

    int case_idx = 0;
    bool all_passed = true;
    int max_time_ms = 0, max_mem_kb = 0;

    while (auto* row = mysql_fetch_row(result)) {
        ++case_idx;
        std::string input_data = row[1] ? row[1] : "";
        std::string expected = row[2] ? row[2] : "";

        std::string output;
        std::string run_err;
        int time_ms = 0, mem_kb = 0;

        int ret = run_sandbox(bin_file.string(), input_data, output, run_err,
                              time_ms, mem_kb, task.time_limit, task.memory_limit);

        if (time_ms > max_time_ms) max_time_ms = time_ms;
        if (mem_kb > max_mem_kb) max_mem_kb = mem_kb;

        if (ret == -1) {
            update_status(task.submission_id, "RE", case_idx, run_err, time_ms, mem_kb);
            all_passed = false;
            break;
        } else if (ret == 1) {
            update_status(task.submission_id, "TLE", case_idx, "", time_ms, mem_kb);
            all_passed = false;
            break;
        } else if (ret == 2) {
            update_status(task.submission_id, "MLE", case_idx, "", time_ms, mem_kb);
            all_passed = false;
            break;
        } else if (trim_output(output) != trim_output(expected)) {
            update_status(task.submission_id, "WA", case_idx, "", time_ms, mem_kb);
            all_passed = false;
            break;
        }
    }

    mysql_free_result(result);
    pool.release(conn);

    if (all_passed) {
        update_status(task.submission_id, "AC", 0, "", max_time_ms, max_mem_kb);
    }

    fs::remove_all(sandbox_dir);
}

bool ExecutorService::compile(const std::string& src_file, const std::string& bin_file,
                               std::string& error_msg) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        int timeout = Config::instance().judge().compile_timeout;
        struct rlimit rl;
        rl.rlim_cur = timeout;
        rl.rlim_max = timeout + 5;
        setrlimit(RLIMIT_CPU, &rl);

        rl.rlim_cur = 256ULL * 1024 * 1024;
        rl.rlim_max = 512ULL * 1024 * 1024;
        setrlimit(RLIMIT_AS, &rl);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }

        execlp("g++", "g++", "-std=c++17", "-O2", "-o", bin_file.c_str(),
               src_file.c_str(), nullptr);
        _exit(1);
    }

    close(pipefd[1]);

    std::vector<char> buf(4096);
    std::string err;
    ssize_t n;
    while ((n = read(pipefd[0], buf.data(), buf.size())) > 0) {
        err.append(buf.data(), n);
    }
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        if (sig == SIGXCPU || sig == SIGKILL)
            error_msg = "Compilation killed: resource limit exceeded (timeout or memory)";
        else
            error_msg = "Compilation killed by signal " + std::to_string(sig);
        if (!err.empty())
            error_msg += "\n" + err;
    } else if (!err.empty()) {
        error_msg = std::move(err);
    } else {
        error_msg = "Compilation failed with exit code " + std::to_string(WEXITSTATUS(status));
    }
    return false;
}

static bool setup_seccomp_filter() {
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
    if (!ctx) return false;

    const int allowed[] = {
        SCMP_SYS(read), SCMP_SYS(write), SCMP_SYS(writev),
        SCMP_SYS(close), SCMP_SYS(mmap), SCMP_SYS(munmap),
        SCMP_SYS(mprotect), SCMP_SYS(brk),
        SCMP_SYS(exit), SCMP_SYS(exit_group),
        SCMP_SYS(fstat), SCMP_SYS(lseek),
        SCMP_SYS(clock_gettime), SCMP_SYS(getrandom),
        SCMP_SYS(rt_sigaction), SCMP_SYS(rt_sigprocmask),
        SCMP_SYS(ioctl), SCMP_SYS(futex),
        SCMP_SYS(nanosleep), SCMP_SYS(clock_nanosleep),
        SCMP_SYS(newfstatat), SCMP_SYS(getdents64),
        SCMP_SYS(dup), SCMP_SYS(dup2),
        SCMP_SYS(openat),
        SCMP_SYS(arch_prctl),
        SCMP_SYS(set_tid_address),
        SCMP_SYS(set_robust_list),
        SCMP_SYS(prlimit64),
        SCMP_SYS(madvise),
        SCMP_SYS(readlink),
        SCMP_SYS(access),
        SCMP_SYS(rseq),
        SCMP_SYS(pread64),
        SCMP_SYS(execve),
    };

    for (int syscall : allowed) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall, 0) < 0) {
            seccomp_release(ctx);
            return false;
        }
    }

    bool ok = seccomp_load(ctx) == 0;
    seccomp_release(ctx);
    return ok;
}

int ExecutorService::run_sandbox(
    const std::string& bin_file, const std::string& input_str,
    std::string& output, std::string& error_msg,
    int& time_used_ms, int& mem_used_kb,
    int time_limit_sec, int mem_limit_mb)
{
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        prctl(PR_SET_PDEATHSIG, SIGKILL);
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

        struct rlimit rl;
        rl.rlim_cur = time_limit_sec;
        rl.rlim_max = time_limit_sec + 1;
        setrlimit(RLIMIT_CPU, &rl);

        long long mem_bytes = (long long)mem_limit_mb * 1024 * 1024;
        rl.rlim_cur = mem_bytes;
        rl.rlim_max = mem_bytes;
        setrlimit(RLIMIT_AS, &rl);

        rl.rlim_cur = Config::instance().judge().output_limit;
        rl.rlim_max = Config::instance().judge().output_limit;
        setrlimit(RLIMIT_FSIZE, &rl);

        rl.rlim_cur = 0;
        rl.rlim_max = 0;
        setrlimit(RLIMIT_NPROC, &rl);

        // setup_seccomp_filter();
        setup_seccomp_filter();

        execl(bin_file.c_str(), bin_file.c_str(), nullptr);
        _exit(1);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    if (!input_str.empty()) {
        write(stdin_pipe[1], input_str.data(), input_str.size());
    }
    close(stdin_pipe[1]);

    auto start = std::chrono::steady_clock::now();

    int status = 0;
    pid_t wpid;
    int timeout_ms = (time_limit_sec + 1) * 1000;
    int elapsed = 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

        if (elapsed >= timeout_ms) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            time_used_ms = elapsed;
            return 1;
        }

        wpid = waitpid(pid, &status, WNOHANG);
        if (wpid == pid) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = std::chrono::steady_clock::now();
    time_used_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    struct rusage usage;
    getrusage(RUSAGE_CHILDREN, &usage);
    mem_used_kb = usage.ru_maxrss;

    std::vector<char> out_buf(4096);
    std::string out_str;
    ssize_t n;
    while ((n = read(stdout_pipe[0], out_buf.data(), out_buf.size())) > 0) {
        out_str.append(out_buf.data(), n);
    }
    close(stdout_pipe[0]);

    std::vector<char> err_buf(4096);
    std::string err_str;
    while ((n = read(stderr_pipe[0], err_buf.data(), err_buf.size())) > 0) {
        err_str.append(err_buf.data(), n);
    }
    close(stderr_pipe[0]);

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        if (sig == SIGXCPU || sig == SIGKILL) {
            return 1;
        }
        if (sig == SIGSEGV || sig == SIGABRT || sig == SIGFPE) {
            error_msg = "Runtime error (signal " + std::to_string(sig) + ")";
            return -1;
        }
        error_msg = "Runtime error (signal " + std::to_string(sig) + ")";
        return -1;
    }

    if (WEXITSTATUS(status) != 0) {
        error_msg = "Non-zero exit code: " + std::to_string(WEXITSTATUS(status));
        return -1;
    }

    output = std::move(out_str);
    return 0;
}

void ExecutorService::update_status(int submission_id, const std::string& status,
                                     int failed_case, const std::string& error_msg,
                                     int time_used, int memory_used) {
    auto& pool = ConnectionPool::instance();
    auto* conn = pool.get();
    if (!conn) return;

    std::string q = "UPDATE submissions SET status='" + status + "'";

    if (failed_case > 0) {
        q += ", failed_case=" + std::to_string(failed_case);
    }

    if (!error_msg.empty()) {
        std::vector<char> buf(error_msg.length() * 2 + 1);
        mysql_real_escape_string(conn, buf.data(), error_msg.data(), error_msg.length());
        q += ", error_msg='" + std::string(buf.data()) + "'";
    }

    if (time_used > 0) {
        q += ", time_used=" + std::to_string(time_used);
    }

    if (memory_used > 0) {
        q += ", memory_used=" + std::to_string(memory_used);
    }

    q += " WHERE id=" + std::to_string(submission_id);

    if (mysql_query(conn, q.c_str()) != 0) {
        Logger::instance().error("update_status failed: " + std::string(mysql_error(conn)));
    }

    pool.release(conn);
    Logger::instance().info("Submission " + std::to_string(submission_id) + " → " + status);
}

std::string ExecutorService::trim_output(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    size_t i = 0;

    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        while (i < s.size() && s[i] != '\n') {
            result += s[i];
            ++i;
        }
        while (!result.empty() && (result.back() == ' ' || result.back() == '\t')) {
            result.pop_back();
        }
        if (i < s.size() && s[i] == '\n') {
            result += '\n';
            ++i;
        }
    }

    while (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }

    return result;
}
