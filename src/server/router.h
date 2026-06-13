#pragma once

namespace httplib {
class Server;
}

void register_routes(httplib::Server& server);
