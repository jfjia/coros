#include "coros.hpp"
#include "malog.h"
#include <thread>

#define USE_SCHEDULERS 1

#ifdef USE_SCHEDULERS
#define NUM_WORKERS 2
#endif

void MyCoFn() {
  coros::Coroutine* c = coros::Coroutine::Self();

  MALOG_INFO("coro-" << c->GetId() << ": MyCoFn()");

  MALOG_INFO("coro-" << c->GetId() << ": wait()");
  c->Wait(500);

  c->BeginCompute();
  MALOG_INFO("coro-" << c->GetId() << ": run in compute thread-" << std::this_thread::get_id());
  c->EndCompute();

  MALOG_INFO("coro-" << c->GetId() << ": back in coro thread-" << std::this_thread::get_id());
}

void ExitFn(coros::Coroutine* c) {
  MALOG_INFO("coro-" << c->GetId() << ": ExitFn()");
}

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);

#ifdef USE_SCHEDULERS
  coros::Schedulers scheds(NUM_WORKERS);
#else
  coros::Scheduler sched(true);
#endif
  for (int i = 0; i < 100; i++) {
#ifdef USE_SCHEDULERS
    /*coros::Coroutine* c = */coros::Coroutine::Create(scheds.GetNext(), MyCoFn, ExitFn);
#else
    co->Start(&sched);
    /*coros::Coroutine* c = */coros::Coroutine::Create(&sched, MyCoFn, ExitFn);
#endif
  }
#ifdef USE_SCHEDULERS
  scheds.Run();
#else
  sched.Run();
#endif

  return 0;
}
