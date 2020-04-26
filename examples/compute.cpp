#include "coros.hpp"
#include "malog.h"
#include <thread>

#define USE_SCHEDULERS 1

#ifdef USE_SCHEDULERS
#define NUM_WORKERS 2
#endif

class MyCo {
public:
  bool Start(coros::Scheduler* sched) {
    return coro_.Create(sched, std::bind(&MyCo::Fn, this), std::bind(&MyCo::ExitFn, this));
  }

  void Fn() {
    MALOG_INFO("coro-" << coro_.GetId() << ": fn()");
    coro_.Wait(500);
    MALOG_INFO("coro-" << coro_.GetId() << ": wait()");
    coro_.BeginCompute();
    MALOG_INFO("coro-" << coro_.GetId() << ": run in compute thread-" << std::this_thread::get_id());
    coro_.EndCompute();
    MALOG_INFO("coro-" << coro_.GetId() << ": back in coro thread-" << std::this_thread::get_id());
  }

  void ExitFn() {
    MALOG_INFO("coro-" << coro_.GetId() << ": exit_fn()");
    delete this;
  }

private:
  coros::Coroutine coro_;
};

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);

#ifdef USE_SCHEDULERS
  coros::Schedulers scheds(NUM_WORKERS);
#else
  coros::Scheduler sched(true);
#endif
  for (int i = 0; i < 100; i++) {
    MyCo* co = new MyCo();
#ifdef USE_SCHEDULERS
    co->Start(scheds.GetNext());
#else
    co->Start(&sched);
#endif
  }
#ifdef USE_SCHEDULERS
  scheds.Run();
#else
  sched.Run();
#endif

  return 0;
}
