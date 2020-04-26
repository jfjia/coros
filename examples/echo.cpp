#include "coros.hpp"
#include "malog.h"
#include <string.h>
#include <memory.h>
#include <thread>

#define USE_SCHEDULERS 1

#ifdef USE_SCHEDULERS
#define NUM_WORKERS 2
#endif

class Conn {
public:
  bool Start(coros::Scheduler* sched, uv_os_sock_t fd) {
    if (!coro_.Create(sched, std::bind(&Conn::Fn, this, fd), std::bind(&Conn::ExitFn, this))) {
      return false;
    }
    return true;
  }

  void Fn(uv_os_sock_t fd) {
    MALOG_INFO("coro-" << coro_.GetId() << ": enter");
    coros::Socket s(fd);
    char buf[256];
    s.SetDeadline(30);
    for (;;) {
      int len = s.ReadSome(buf, 256);
      if (len <= 0) {
        MALOG_INFO("coro-" << coro_.GetId() << ": read fail " << len);
        break;
      }
      len = s.WriteExactly(buf, len);
      if (len <= 0) {
        MALOG_ERROR("coro-" << coro_.GetId() << ": conn broken");
        break;
      }
      if (strncmp(buf, "exit", 4) == 0) {
        break;
      }
      // If socket is always readable/writable, we may
      // enter tight loop then block libuv event loop
      // and other coroutines. Suspend currnet coroutine
      // manually.
      coro_.Nice();
    }
    s.Close();
  }

  void ExitFn() {
    MALOG_INFO("coro-" << coro_.GetId() << ": exit");
    delete this;
  }

private:
  coros::Coroutine coro_;
  std::string id_;
};

class Listener {
public:
#ifdef USE_SCHEDULERS
  bool Start(coros::Schedulers* scheds) {
    scheds_ = scheds;
    if (!coro_.Create(scheds->GetDefault(), std::bind(&Listener::Fn, this), std::bind(&Listener::ExitFn, this))) {
#else
  bool Start(coros::Scheduler* sched) {
    sched_ = sched;
    if (!coro_.Create(sched, std::bind(&Listener::Fn, this), std::bind(&Listener::ExitFn, this))) {
#endif
      return false;
    }
    return true;
  }

  void Fn() {
    MALOG_INFO("coro-" << coro_.GetId() << ": enter");
    coros::Socket s;
    s.ListenByIp("0.0.0.0", 9090);
    for (;;) {
      uv_os_sock_t s_new = s.Accept();
      MALOG_INFO("coro-" << coro_.GetId() << ": accept new conn");
      if (s_new == BAD_SOCKET) {
        break;
      }
      Conn* c = new Conn();
#ifdef USE_SCHEDULERS
      c->Start(scheds_->GetNext(), s_new);
#else
      c->Start(sched, s_new);
#endif
    }
  }

  void ExitFn() {
    MALOG_INFO("coro-" << coro_.GetId() << ": exit");
  }

protected:
  coros::Coroutine coro_;
#ifdef USE_SCHEDULERS
  coros::Schedulers* scheds_;
#else
  coro::Scheduler* sched_;
#endif
};

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);
#ifdef USE_SCHEDULERS
  coros::Schedulers scheds(NUM_WORKERS);
#else
  coros::Scheduler sched(true);
#endif

  Listener l;
#ifdef USE_SCHEDULERS
  l.Start(&scheds);
  scheds.Run();
#else
  l.Start(&sched);
  sched.Run();
#endif

  return 0;
}
