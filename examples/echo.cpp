#include "coros.hpp"
#include "malog.h"
#include <string.h>
#include <memory.h>
#include <thread>

#define USE_SCHEDULERS 1

#ifdef USE_SCHEDULERS
#define NUM_WORKERS 2
#endif

void ConnFn(uv_os_sock_t fd) {
  coros::Coroutine* c = coros::Coroutine::Self();

  MALOG_INFO("coro-" << c->GetId() << ": enter");
  coros::Socket s(fd);
  char buf[256];
  s.SetDeadline(30);
  for (;;) {
    int len = s.ReadSome(buf, 256);
    if (len <= 0) {
      MALOG_INFO("coro-" << c->GetId() << ": read fail " << len);
      break;
    }
    len = s.WriteExactly(buf, len);
    if (len <= 0) {
      MALOG_ERROR("coro-" << c->GetId() << ": conn broken");
      break;
    }
    if (strncmp(buf, "exit", 4) == 0) {
      break;
    }
  }
  s.Close();
}

void ExitFn(coros::Coroutine* c) {
  MALOG_INFO("coro-" << c->GetId() << ": exit");
}

#ifdef USE_SCHEDULERS
void ListenerFn(coros::Schedulers* scheds) {
#else
void ListenerFn(coros::Scheduler* sched) {
#endif
  coros::Coroutine* c = coros::Coroutine::Self();
  MALOG_INFO("coro-" << c->GetId() << ": enter");
  coros::Socket s;
  s.ListenByIp("0.0.0.0", 9090);
  for (;;) {
    uv_os_sock_t s_new = s.Accept();
    MALOG_INFO("coro-" << c->GetId() << ": accept new conn");
    if (s_new == BAD_SOCKET) {
      break;
    }
#ifdef USE_SCHEDULERS
    /*coros::Coroutine* c_new = */coros::Coroutine::Create(scheds->GetNext(), std::bind(ConnFn, s_new), ExitFn);
#else
    /*coros::Coroutine* c_new = */coros::Coroutine::Create(sched, std::bind(ConnFn, s_new), ExitFn);
#endif
  }
}

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);
#ifdef USE_SCHEDULERS
  coros::Schedulers scheds(NUM_WORKERS);
#else
  coros::Scheduler sched(true);
#endif

#ifdef USE_SCHEDULERS
  /*coros::Coroutine* c = */coros::Coroutine::Create(scheds.GetDefault(), std::bind(ListenerFn, &scheds), ExitFn);
  scheds.Run();
#else
  /*coros::Coroutine* c = */coros::Coroutine::Create(&sched, std::bind(ListenerFn, &sched), ExitFn);
  sched.Run();
#endif

  return 0;
}
