#include "coros.hpp"
#include "malog.h"
#include <string.h>
#include <memory.h>

class Conn {
public:
  bool Start(uv_os_sock_t fd) {
    if (!coro_.Create(coros::Scheduler::Get(), std::bind(&Conn::Fn, this, fd), std::bind(&Conn::ExitFn, this))) {
      return false;
    }
    return true;
  }

  void Fn(uv_os_sock_t fd) {
    coros::Socket s(fd);
    char buf[256];
    s.SetDeadline(30);
    for (;;) {
      int len = s.ReadSome(buf, 256);
      MALOG_INFO("coro[" << coro_.GetId() << "]: in " << len << " bytes");
      if (len <= 0) {
        MALOG_INFO("coro[" << coro_.GetId() << "] read fail: " << len);
        break;
      }
      len = s.WriteSome(buf, len);
      MALOG_INFO("coro[" << coro_.GetId() << "] out " << len << "bytes");
      if (strncmp(buf, "exit", 4) == 0) {
        break;
      }
    }
    s.Close();
  }

  void ExitFn() {
    MALOG_INFO("coro[" << coro_.GetId() << "] exit");
    delete this;
  }

private:
  coros::Coroutine coro_;
};

class Listener {
public:
  bool Start() {
    if (!coro_.Create(coros::Scheduler::Get(), std::bind(&Listener::Fn, this), std::bind(&Listener::ExitFn, this))) {
      return false;
    }
    return true;
  }

  void Fn() {
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

  void ExitFn() {
  }

protected:
  coros::Coroutine coro_;
};

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, 0, true);
  coros::Scheduler sched(true);
  Listener l;
  l.Start();
  sched.Run();
  return 0;
}
