#include "router.h"
#include "../handler/auth_handler.hpp"
#include "../handler/problem_handler.hpp"
#include "../handler/submit_handler.hpp"
#include "../handler/admin_handler.hpp"

#include <httplib.h>

void register_routes(httplib::Server& server) {
    server.Post("/api/register", handle_register);
    server.Post("/api/login", handle_login);
    server.Post("/api/logout", handle_logout);
    server.Get("/api/me", handle_me);

    server.Get("/api/problems", handle_get_problems);
    server.Get("/api/problems/:id", handle_get_problem);
    server.Post("/api/problems", handle_create_problem);
    server.Put("/api/problems/:id", handle_update_problem);
    server.Delete("/api/problems/:id", handle_delete_problem);

    server.Post("/api/submit", handle_submit);
    server.Get("/api/submissions/:id", handle_get_submission);
    server.Get("/api/submissions", handle_get_submissions);

    server.Get("/api/problems/:id/testcases", handle_get_testcases);
    server.Post("/api/problems/:id/testcases", handle_add_testcase);
    server.Delete("/api/problems/:id/testcases/:tc_id", handle_delete_testcase);
}
