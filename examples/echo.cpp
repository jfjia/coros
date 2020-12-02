#include "coros.h"
#include "malog.h"
#include <string.h>
#include <memory.h>
#include <thread>
#include <sstream>

#define USE_SCHEDULERS 1

#ifdef USE_SCHEDULERS
static const int kNumWorkers = 2;
#endif

std::string GetId(coros::Coroutine* c) {
  std::stringstream ss;
  ss << "coro[" << c->GetId() << "]";
  return ss.str();
}

void ConnFn(uv_os_sock_t fd) {
  coros::Coroutine* c = coros::Coroutine::Self();

  std::string id = GetId(c);

  MALOG_INFO(id << ": new coror enter for fd=" << fd);
  coros::Socket s(fd);
  char buf[256];
  s.SetDeadline(30);
  for (;;) {
    int len = s.ReadSome(buf, 256);
    if (len <= 0) {
      MALOG_INFO(id << ": read fail " << len);
      break;
    }
    len = s.WriteExactly(buf, len);
    if (len <= 0) {
      MALOG_ERROR(id << ": conn broken");
      break;
    }
    if (len >= 4 && strncmp(buf, "exit", 4) == 0) {
      break;
    }
  }
  s.Close();
}

void ExitFn(coros::Coroutine* c) {
  std::string id = GetId(c);

  MALOG_INFO(id << ": exit");
}

#ifdef USE_SCHEDULERS
void ListenerFn(coros::Schedulers* scheds) {
#else
void ListenerFn(coros::Scheduler* sched) {
#endif
  coros::Coroutine* c = coros::Coroutine::Self();

  std::string id = GetId(c);

  MALOG_INFO(id << ": new coro enter for listener");
  coros::Socket s;
  s.ListenByIp("0.0.0.0", 9090);
  MALOG_INFO(id << ": listening 0.0.0.0:9090");
  for (;;) {
    uv_os_sock_t s_new = s.Accept();
    MALOG_INFO(id << ": accept new conn fd=" << s_new);
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

  coros::Scheduler sched(true);
#ifdef USE_SCHEDULERS
  coros::Schedulers scheds(kNumWorkers);
#endif

#ifdef USE_SCHEDULERS
  /*coros::Coroutine* c = */coros::Coroutine::Create(&sched, std::bind(ListenerFn, &scheds), ExitFn);
#else
  /*coros::Coroutine* c = */coros::Coroutine::Create(&sched, std::bind(ListenerFn, &sched), ExitFn);
#endif

  sched.Run();

  return 0;
}
