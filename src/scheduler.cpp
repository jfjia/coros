#include "coros.hpp"
#include "malog.h"
#include <cassert>
#include <cmath>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#if !defined(_WIN32)
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

namespace coros {

static const int SWEEP_INTERVAL = 1000;
static const int RECV_BUFFER_SIZE = 64 * 1024;
static const int COMPUTE_THREADS_N = 2;

std::size_t Scheduler::page_size_;
std::size_t Scheduler::min_stack_size_;
std::size_t Scheduler::max_stack_size_;
std::size_t Scheduler::default_stack_size_;

thread_local Scheduler* local_sched = nullptr;

class ComputeThreads {
public:
  void start();
  void stop();
  void add(Coroutine* coro);

protected:
  void consume();

protected:
  bool stop_{ false };
  std::vector<std::thread> threads_;
  std::mutex lock_;
  std::condition_variable cond_;
  CoroutineList pending_;
};

ComputeThreads compute_threads;

Scheduler::Scheduler(bool is_default)
  : is_default_(is_default) {
  if (is_default) {
#if defined(_WIN32)
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    page_size_ = (std::size_t)si.dwPageSize;

    min_stack_size_ = 8 * 1024;
    max_stack_size_ = 1024 * 1024;
    default_stack_size_ = 32 * 1024;
#else
#if defined(SIGSTKSZ)
    default_stack_size_ = SIGSTKSZ;
#else
    default_stack_size_ = 32 * 1024;
#endif
#if defined(MINSIGSTKSZ)
    min_stack_size_ = MINSIGSTKSZ;
#else
    min_stack_size_ = 8 * 1024;
#endif
    page_size_ = (size_t)sysconf(_SC_PAGESIZE);
    struct rlimit limit;
    getrlimit(RLIMIT_STACK, &limit);
    max_stack_size_ = (size_t)limit.rlim_max;
#endif
    loop_.init(true);
    compute_threads.start();
  } else {
    loop_.init(false);
  }

  pre_.init(loop_);
  pre_.start(std::bind(&Scheduler::pre, this));

  check_.init(loop_);
  check_.start(std::bind(&Scheduler::check, this));

  async_.init(loop_, std::bind(&Scheduler::async, this));

  sweep_timer_.init(loop_);
  sweep_timer_.start(std::bind(&Scheduler::sweep, this), SWEEP_INTERVAL, SWEEP_INTERVAL);

  local_sched = this;
}

void Scheduler::pre() {
  check();
}

template<typename T>
inline void fast_del_vector_item(std::vector<T>& v, std::size_t i) {
  assert(i >= 0 && i < v.size());
  v[i] = v[v.size() - 1];
  v.pop_back();
}

void Scheduler::check() {
  for (std::size_t i = 0; i < waiting_.size();) {
    Coroutine* c = waiting_[i];
    if (c->state() == STATE_READY) {
      ready_.push_back(c);
      fast_del_vector_item<Coroutine* >(waiting_, i);
      continue;
    }
    i++;
  }
  run_coros();
}

void Scheduler::async() {
  std::lock_guard<std::mutex> l(lock_);
  ready_.insert(ready_.end(), posted_.begin(), posted_.end());
  posted_.clear();
  ready_.insert(ready_.end(), compute_done_.begin(), compute_done_.end());
  outstanding_ -= compute_done_.size();
  compute_done_.clear();
}

void Scheduler::sweep() {
  for (std::size_t i = 0; i < waiting_.size();) {
    Coroutine* c = waiting_[i];
    c->check_timeout();
    if (c->state() == STATE_READY) {
      ready_.push_back(c);
      fast_del_vector_item<Coroutine* >(waiting_, i);
      continue;
    }
    i++;
  }
}

Scheduler::~Scheduler() {
  uv_loop_close(loop_.value());
  if (is_default_) {
    compute_threads.stop();
#if defined(_WIN32)
    WSACleanup();
#endif
  }
}

std::size_t Scheduler::next_coro_id() {
  static std::atomic<std::size_t> next_id{ 1 };
  return next_id.fetch_add(1);
}

void Scheduler::add_coroutine(Coroutine* coro) {
  switch (coro->state()) {
  case STATE_READY:
    ready_.push_back(coro);
    break;
  case STATE_WAITING:
    waiting_.push_back(coro);
    break;
  case STATE_DONE:
    coro->destroy();
    break;
  default:
    assert(false);
    break;
  }
}

void Scheduler::run() {
  loop_.run();
  cleanup(ready_);
  cleanup(waiting_);
  sweep_timer_.stop();
  check_.stop();
  pre_.stop();
  sweep_timer_.close();
  async_.close();
  check_.close();
  pre_.close();
  loop_.runNowait();
}

void Scheduler::run_coros() {
  while (ready_.size() > 0) {
    for (std::size_t i = 0; i < ready_.size();) {
      Coroutine* c = ready_[i];
      current_ = c;
      c->resume();
      current_ = nullptr;
      if (c->state() == STATE_DONE) {
        c->destroy();
        fast_del_vector_item<Coroutine*>(ready_, i);
        continue;
      } else if (c->state() == STATE_WAITING) {
        waiting_.push_back(c);
        fast_del_vector_item<Coroutine*>(ready_, i);
        continue;
      } else if (c->state() == STATE_COMPUTE) {
        fast_del_vector_item<Coroutine*>(ready_, i);
        outstanding_ ++;
        compute_threads.add(c);
      }
      i++;
    }
  }
  if (waiting_.size() == 0 && outstanding_ == 0) {
    uv_stop(loop_.value());
  }
}

void Scheduler::wait(Coroutine* coro, long millisecs) {
  uv_timer_t timer;
  timer.data = (void*)coro;
  uv_timer_init(loop_.value(), &timer);
  uv_timer_start(&timer, [](uv_timer_t* w) {
    uv_timer_stop(w);
    uv_close(reinterpret_cast<uv_handle_t*>(w), [](uv_handle_t* w) {
      ((Coroutine*)w->data)->set_event(EVENT_TIMEOUT);
    });
  }, millisecs, 0);
  coro->yield(STATE_WAITING);
}

void Scheduler::wait(Coroutine* coro, Socket& s, int flags) {
  int events = 0;
  if (flags & WAIT_READABLE) {
    events |= UV_READABLE;
  }
  if (flags & WAIT_WRITABLE) {
    events |= UV_WRITABLE;
  }
  events |= UV_DISCONNECT;
  uv_poll_start(&s.poll_, events, [](uv_poll_t* w, int status, int events) {
    MALOG_INFO("status: " << status << ", events=" << events);
    if (status > 0) {
      ((Socket*)w->data)->coro_->set_event(EVENT_HUP);
    } else if ((events & UV_READABLE) && (events & UV_WRITABLE)) {
      ((Socket*)w->data)->coro_->set_event(EVENT_RWABLE);
    } else if (events & UV_READABLE) {
      ((Socket*)w->data)->coro_->set_event(EVENT_READABLE);
    } else if (events & UV_WRITABLE) {
      ((Socket*)w->data)->coro_->set_event(EVENT_WRITABLE);
    } else if (events & UV_DISCONNECT) {
      ((Socket*)w->data)->coro_->set_event(EVENT_HUP);
    } else {
      //???TODO
    }
  });
  s.coro_->set_timeout(s.get_deadline());
  s.coro_->yield(STATE_WAITING);
  uv_poll_stop(&s.poll_);
}

void Scheduler::cleanup(CoroutineList& cl) {
  for (auto c : cl) {
    c->set_event(EVENT_CANCEL);
    c->resume();
    c->destroy();
  }
  cl.clear();
}

void Scheduler::post_coroutine(Coroutine* coro, bool is_compute) {
  {
    std::lock_guard<std::mutex> l(lock_);
    if (is_compute) {
      compute_done_.push_back(coro);
    } else {
      posted_.push_back(coro);
    }
  }
  async_.send();
}

void Scheduler::begin_compute(Coroutine* coro) {
  /*uv_work_t c;
  c.data = (void*)coro;
  uv_queue_work(loop_.value(), &c, [](uv_work_t* w) {
      ((Coroutine*)w->data)->set_event(EVENT_COMPUTE);
      ((Coroutine*)w->data)->resume();
  }, [](uv_work_t* w, int status) {
      ((Coroutine*)w->data)->set_event(EVENT_COMPUTE_DONE);
      ((Coroutine*)w->data)->sched()->add_coroutine((Coroutine*)w->data);
      ((Coroutine*)w->data)->sched()->outstanding_ --;
  });
  */
  coro->yield(STATE_COMPUTE);
}

Stack Scheduler::allocate_stack() {
  const std::size_t pages(static_cast<std::size_t>(std::ceil(static_cast<float>(default_stack_size_) / page_size_)));
  const std::size_t size__ = (pages + 1) * page_size_;
  Stack stack;
  void* vp = nullptr;

#if defined(_WIN32)
  vp = ::VirtualAlloc(0, size__, MEM_COMMIT, PAGE_READWRITE);
  if (! vp) {
    return stack;
  }
  DWORD old_options;
  ::VirtualProtect(vp, page_size_, PAGE_READWRITE | PAGE_GUARD /*PAGE_NOACCESS*/, &old_options);
#else
# if defined(MAP_ANON)
  vp = mmap(0, size__, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
# else
  vp = mmap(0, size__, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
# endif
  if (vp == MAP_FAILED) {
    return stack;
  }
  mprotect(vp, page_size_, PROT_NONE);
#endif

  stack.size = size__;
  stack.sp = static_cast< char* >(vp) + stack.size;
  return stack;
}

void Scheduler::deallocate_stack(Stack& stack) {
  assert(stack.sp);
  void* vp = static_cast< char* >(stack.sp) - stack.size;
#if defined(_WIN32)
  ::VirtualFree(vp, 0, MEM_RELEASE);
#else
  munmap(vp, stack.size);
#endif
}

Scheduler* Scheduler::get() {
  return local_sched;
}

void ComputeThreads::start() {
  for (int i = 0; i < COMPUTE_THREADS_N; i++) {
    threads_.emplace_back(std::bind(&ComputeThreads::consume, this));
  }
}

void ComputeThreads::stop() {
  {
    std::lock_guard<std::mutex> l(lock_);
    stop_ = true;
    cond_.notify_all();
  }
  for (std::size_t i = 0; i < threads_.size(); i++) {
    if (threads_[i].joinable()) {
      threads_[i].join();
    }
  }
  threads_.clear();
  for (auto i : pending_) {
    i->set_event(EVENT_CANCEL);
    i->resume();
    i->destroy();
  }
  pending_.clear();
}

inline void ComputeThreads::add(Coroutine* coro) {
  std::lock_guard<std::mutex> l(lock_);
  pending_.push_back(coro);
  cond_.notify_all();
}

void ComputeThreads::consume() {
  Coroutine* coro;
  while (true) {
    {
      std::unique_lock<std::mutex> l(lock_);
      if (stop_) {
        break;
      }
      if (pending_.size() == 0) {
        cond_.wait(l);
        continue;
      }
      coro = pending_.back();
      pending_.pop_back();
    }
    coro->set_event(EVENT_COMPUTE);
    coro->resume();
    coro->sched()->post_coroutine(coro, true);
  }
}

}
