#include "coros.hpp"
#include "malog.h"
#include <thread>

#define NUM_WORKERS 2

class MyCo {
public:
  bool Start(coros::Scheduler* sched) {
    return coro_.Create(sched, std::bind(&MyCo::Fn, this), std::bind(&MyCo::ExitFn, this));
  }

  void Fn() {
    MALOG_INFO("coro[" << coro_.GetId() << "]: fn()");
    coro_.Wait(500);
    MALOG_INFO("coro[" << coro_.GetId() << "]: wait()");
    coro_.BeginCompute();
    MALOG_INFO("coro[" << coro_.GetId() << "]: run in compute thread-" << std::this_thread::get_id());
    coro_.EndCompute();
    MALOG_INFO("coro[" << coro_.GetId() << "]: back in coro thread-" << std::this_thread::get_id());
  }

  void ExitFn() {
    MALOG_INFO("coro[" << coro_.GetId() << "]: exit_fn()");
    delete this;
  }

private:
  coros::Coroutine coro_;
};

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);

  coros::Schedulers<NUM_WORKERS> scheds;
  scheds.Start();
  for (int i = 0; i < 100; i++) {
    MyCo* co = new MyCo();
    co->Start(scheds.GetNext());
  }
  scheds.Run();
  scheds.Stop();
  return 0;
}
