#include "malog.h"
#include "rtmp_server.hpp"
#include "rtmp_conn.hpp"

bool Server::Start() {
  if (!coro_.Create(coros::Scheduler::Get(),
                    std::bind(&Server::Fn, this),
                    std::bind(&Server::ExitFn, this))) {
    return false;
  }
  return true;
}

void Server::Fn() {
  coros::Socket s;
  s.ListenByIp("0.0.0.0", 9090);
  for (;;) {
    uv_os_sock_t s_new = s.Accept();
    MALOG_INFO("coro[" << coro_.GetId() << ": accept new " << s_new);
    if (s_new == BAD_SOCKET) {
      break;
    }
    Conn* c = new Conn();
    c->Start(s_new);
  }
}

void Server::ExitFn() {
}
