#include "coros.h"
#include "malog.h"
#include <thread>
#include <sstream>

#define N_COROS 100

#define USE_SCHEDULERS 1

#ifdef USE_SCHEDULERS
#define NUM_WORKERS 2
#include <atomic>
std::atomic_int n_coros(N_COROS);
#endif

std::string GetId(coros::Coroutine* c) {
  std::stringstream ss;
  ss << "coro[" << c->GetId() << "]";
  return ss.str();
}

void MyCoFn() {
  coros::Coroutine* c = coros::Coroutine::Self();

  std::string id = GetId(c);

  MALOG_INFO(id << ": MyCoFn() in coro thread-" << std::this_thread::get_id());

  c->BeginCompute();
  MALOG_INFO(id << ": run in compute thread-" << std::this_thread::get_id());
  // Do some block operations
  c->EndCompute();

  MALOG_INFO(id << ": back in coro thread-" << std::this_thread::get_id());
}

void ExitFn(coros::Coroutine* c) {
  std::string id = GetId(c);

  MALOG_INFO(id << ": ExitFn()");
  n_coros --;
}

#ifdef USE_SCHEDULERS
void GuardFn(coros::Schedulers* scheds) {
  coros::Coroutine* c = coros::Coroutine::Self();
  for (;;) {
    c->Wait(500);
    if (n_coros <= 0) {
      break;
    }
  }
  scheds->Stop();
}

void GuardExitFn(coros::Coroutine* c) {
}
#endif

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);

  coros::Scheduler sched(true);
#ifdef USE_SCHEDULERS
  coros::Schedulers scheds(NUM_WORKERS);
  /*coros::Coroutine* c_guard = */coros::Coroutine::Create(&sched, std::bind(GuardFn, &scheds), GuardExitFn);
#endif

  for (int i = 0; i < N_COROS; i++) {
#ifdef USE_SCHEDULERS
    /*coros::Coroutine* c = */coros::Coroutine::Create(scheds.GetNext(), MyCoFn, ExitFn);
#else
    /*coros::Coroutine* c = */coros::Coroutine::Create(&sched, MyCoFn, ExitFn);
#endif
  }

  sched.Run();

  return 0;
}
