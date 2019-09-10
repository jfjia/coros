#include "coros.hpp"
#include "malog.h"

class MyCo {
public:
  bool Start(coros::Scheduler* sched) {
    return coro_.Create(sched, std::bind(&MyCo::Fn, this), std::bind(&MyCo::ExitFn, this));
  }

  void Fn() {
    malog_info("coro[%lld]: fn()", coro_.GetId());
    coro_.Wait(500);
    malog_info("coro[%lld]: wait()", coro_.GetId());
    coro_.BeginCompute();
    malog_info("coro[%lld]: run in compute threads", coro_.GetId());
    coro_.EndCompute();
    malog_info("coro[%lld]: back in coro thread", coro_.GetId());
  }

  void ExitFn() {
    malog_info("coro[%lld]: exit_fn()", coro_.GetId());
    delete this;
  }

private:
  coros::Coroutine coro_;
};

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, 0, true);
  coros::Scheduler sched(true);
  MyCo* co = new MyCo();
  co->Start(&sched);
  sched.Run();
  return 0;
}
