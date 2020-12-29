#include "coros.h"
#include "malog.h"
#include <thread>
#include <sstream>
#include <random>
#include <time.h>
#include <chrono>

static const int kNumCoros = 100;

#define USE_SCHEDULERS 1

#ifdef USE_SCHEDULERS
static const int kNumWorkers = 2;
#include <atomic>
std::atomic_int n_coros(kNumCoros);
#endif

thread_local std::default_random_engine e(time(NULL));
thread_local std::uniform_int_distribution<int> u(100, 500);

std::string GetId(coros::Coroutine* c) {
  std::stringstream ss;
  ss << "coro[" << c->GetId() << "]";
  return ss.str();
}

struct ClsData {
  std::string param1;

  ClsData() {
    param1 = "hello, cls";
  }
};

void MyCoFn() {
  coros::Coroutine* c = coros::Coroutine::Self();

  std::string id = GetId(c);

  ClsData* cls = new (c->GetCls()) ClsData();

  MALOG_INFO(id << ": MyCoFn() in coro thread-" << std::this_thread::get_id() << ", cls=" << cls->param1);

  int msecs = u(e);
  c->Wait(msecs); // This will not block Scheduler thread, just suspend current coroutine

  c->BeginCompute();
  MALOG_INFO(id << ": run in compute thread-" << std::this_thread::get_id());
  // Do some real blocking operations
  // This will not block Scheduler thread as it's running in compute thread pool
  std::this_thread::sleep_for(std::chrono::milliseconds(msecs));
  c->EndCompute();

  cls->~ClsData();
  MALOG_INFO(id << ": back in coro thread-" << std::this_thread::get_id());
}

void ExitFn(coros::Coroutine* c) {
  std::string id = GetId(c);

  MALOG_INFO(id << ": ExitFn()");
  n_coros --;
}

#ifdef USE_SCHEDULERS
void GuardFn(coros::Schedulers* scheds) {
#else
void GuardFn() {
#endif
  coros::Coroutine* c = coros::Coroutine::Self();
  for (;;) {
    c->Wait(500);
    if (n_coros <= 0) {
      break;
    }
  }
  c->GetScheduler()->Stop();
#ifdef USE_SCHEDULERS
  scheds->Stop();
#endif
}

void GuardExitFn(coros::Coroutine* c) {
}

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);

  coros::Scheduler sched(true);
#ifdef USE_SCHEDULERS
  coros::Schedulers scheds(kNumWorkers);
  /*coros::Coroutine* c_guard = */coros::Coroutine::Create(&sched, std::bind(GuardFn, &scheds), GuardExitFn);
#else
  /*coros::Coroutine* c_guard = */coros::Coroutine::Create(&sched, GuardFn, GuardExitFn);
#endif

  for (int i = 0; i < kNumCoros; i++) {
#ifdef USE_SCHEDULERS
    /*coros::Coroutine* c = */coros::Coroutine::Create(scheds.GetNext(), MyCoFn, ExitFn, sizeof(ClsData));
#else
    /*coros::Coroutine* c = */coros::Coroutine::Create(&sched, MyCoFn, ExitFn, sizeof(ClsData));
#endif
  }

  sched.Run();

  return 0;
}
