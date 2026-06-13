#include "server/router.h"
#include "service/auth_service.hpp"
#include "service/problem_service.hpp"
#include "service/executor_service.hpp"
#include "db/connection_pool.hpp"
#include "utils/config.hpp"
#include "utils/logger.hpp"

#include <httplib.h>
#include <iostream>
#include <csignal>

static httplib::Server* g_server = nullptr;

void signal_handler(int) {
    if (g_server) g_server->stop();
}

int main(int argc, char* argv[]) {
    std::string config_path = "config/config.json";
    if (argc > 1) config_path = argv[1];

    if (!Config::instance().load(config_path)) {
        std::cerr << "Failed to load config: " << config_path << std::endl;
        return 1;
    }

    Logger::instance().init("oj_backend.log");

    auto& db_cfg = Config::instance().database();
    if (!ConnectionPool::instance().init(db_cfg)) {
        Logger::instance().error("Failed to initialize database connection pool");
        return 1;
    }

    AuthService::instance().init();
    ProblemService::instance().init();
    ExecutorService::instance().init();

    httplib::Server server;
    g_server = &server;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    auto static_dir = Config::instance().server().static_dir;
    if (!server.set_mount_point("/", static_dir)) {
        Logger::instance().warn("Static directory not found: " + static_dir);
    }

    register_routes(server);

    auto port = Config::instance().server().port;
    Logger::instance().info("Starting server on port " + std::to_string(port));

    if (!server.listen("0.0.0.0", port)) {
        Logger::instance().error("Failed to start server");
        return 1;
    }

    Logger::instance().info("Server stopped");
    ExecutorService::instance().shutdown();
    AuthService::instance().shutdown();
    ConnectionPool::instance().close_all();

    return 0;
}
