#include "coros.hpp"
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

#undef USE_UV_THREADPOOL

namespace coros {

static const int SWEEP_INTERVAL = 1000;
static const int RECV_BUFFER_SIZE = 64 * 1024;
#ifndef USE_UV_THREADPOOL
static const int COMPUTE_THREADS_N = 2;
#endif

std::size_t Scheduler::page_size_;
std::size_t Scheduler::min_stack_size_;
std::size_t Scheduler::max_stack_size_;
std::size_t Scheduler::default_stack_size_;

thread_local Scheduler* local_sched = nullptr;

#ifndef USE_UV_THREADPOOL
class ComputeThreads {
public:
  void Start();
  void Stop();
  void Add(Coroutine* coro);

protected:
  void Consume();

protected:
  bool stop_{ false };
  std::vector<std::thread> threads_;
  std::mutex lock_;
  std::condition_variable cond_;
  CoroutineList pending_;
};

ComputeThreads compute_threads;
#endif

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
    loop_ptr_ = uv_default_loop();
#ifndef USE_UV_THREADPOOL
    compute_threads.Start();
#endif
  } else {
    uv_loop_init(&loop_);
    loop_ptr_ = &loop_;
  }

  loop_ptr_->data = this;

  pre_.data = this;
  uv_prepare_init(loop_ptr_, &pre_);
  uv_prepare_start(&pre_, [](uv_prepare_t* handle) {
    (reinterpret_cast<Scheduler*>(handle->data))->Pre();
  });

  check_.data = this;
  uv_check_init(loop_ptr_, &check_);
  uv_check_start(&check_, [](uv_check_t* handle) {
    (reinterpret_cast<Scheduler*>(handle->data))->Check();
  });

  async_.data = this;
  uv_async_init(loop_ptr_, &async_, [](uv_async_t* handle) {
    (reinterpret_cast<Scheduler*>(handle->data))->Async();
  });

  sweep_timer_.data = this;
  uv_timer_init(loop_ptr_, &sweep_timer_);
  uv_timer_start(&sweep_timer_, [](uv_timer_t* handle) {
    (reinterpret_cast<Scheduler*>(handle->data))->Sweep();
  }, SWEEP_INTERVAL, SWEEP_INTERVAL);

  local_sched = this;
}

void Scheduler::Pre() {
  Check();
}

template<typename T>
inline void FastDelVectorItem(std::vector<T>& v, std::size_t i) {
  assert(i >= 0 && i < v.size());
  v[i] = v[v.size() - 1];
  v.pop_back();
}

void Scheduler::Check() {
  for (std::size_t i = 0; i < waiting_.size();) {
    Coroutine* c = waiting_[i];
    if (c->GetState() == STATE_READY) {
      ready_.push_back(c);
      FastDelVectorItem<Coroutine* >(waiting_, i);
      continue;
    }
    i++;
  }
  RunCoros();
}

void Scheduler::Async() {
  std::lock_guard<std::mutex> l(lock_);
  if (posted_.size() > 0) {
    ready_.insert(ready_.end(), posted_.begin(), posted_.end());
    posted_.clear();
  }
  if (compute_done_.size() > 0) {
    ready_.insert(ready_.end(), compute_done_.begin(), compute_done_.end());
    outstanding_ -= compute_done_.size();
    compute_done_.clear();
  }
}

void Scheduler::Sweep() {
  for (std::size_t i = 0; i < waiting_.size();) {
    Coroutine* c = waiting_[i];
    c->CheckTimeout();
    if (c->GetState() == STATE_READY) {
      ready_.push_back(c);
      FastDelVectorItem<Coroutine* >(waiting_, i);
      continue;
    }
    i++;
  }
}

Scheduler::~Scheduler() {
  uv_loop_close(loop_ptr_);
  if (is_default_) {
#ifndef USE_UV_THREADPOOL
    compute_threads.Stop();
#endif
#if defined(_WIN32)
    WSACleanup();
#endif
  }
}

std::size_t Scheduler::NextId() {
  static std::atomic<std::size_t> next_id{ 1 };
  return next_id.fetch_add(1);
}

void Scheduler::AddCoroutine(Coroutine* coro) {
  switch (coro->GetState()) {
  case STATE_READY:
    ready_.push_back(coro);
    break;
  case STATE_WAITING:
    waiting_.push_back(coro);
    break;
  case STATE_DONE:
    coro->Destroy();
    break;
  default:
    assert(false);
    break;
  }
}

inline void CloseNoCb(void* handle) {
  uv_close(reinterpret_cast<uv_handle_t*>(handle), NULL);
}

void Scheduler::Run() {
  uv_run(loop_ptr_, UV_RUN_DEFAULT);
  Cleanup(ready_);
  Cleanup(waiting_);
  uv_timer_stop(&sweep_timer_);
  uv_check_stop(&check_);
  uv_prepare_stop(&pre_);
  CloseNoCb(&sweep_timer_);
  CloseNoCb(&async_);
  CloseNoCb(&check_);
  CloseNoCb(&pre_);
  uv_run(loop_ptr_, UV_RUN_NOWAIT);
}

void Scheduler::RunCoros() {
  while (ready_.size() > 0) {
    for (std::size_t i = 0; i < ready_.size();) {
      Coroutine* c = ready_[i];
      current_ = c;
      c->Resume();
      current_ = nullptr;
      if (c->GetState() == STATE_DONE) {
        c->Destroy();
        FastDelVectorItem<Coroutine*>(ready_, i);
        continue;
      } else if (c->GetState() == STATE_WAITING) {
        waiting_.push_back(c);
        FastDelVectorItem<Coroutine*>(ready_, i);
        continue;
      } else if (c->GetState() == STATE_COMPUTE) {
        FastDelVectorItem<Coroutine*>(ready_, i);
        outstanding_ ++;
#ifndef USE_UV_THREADPOOL
        compute_threads.Add(c);
#endif
      }
      i++;
    }
  }
  if (waiting_.size() == 0 && outstanding_ == 0) {
    uv_stop(loop_ptr_);
  }
}

void Scheduler::Wait(Coroutine* coro, long millisecs) {
  uv_timer_t timer;
  timer.data = coro;
  uv_timer_init(loop_ptr_, &timer);
  uv_timer_start(&timer, [](uv_timer_t* w) {
    uv_timer_stop(w);
    uv_close(reinterpret_cast<uv_handle_t*>(w), [](uv_handle_t* handle) {
      (reinterpret_cast<Coroutine*>(handle->data))->SetEvent(EVENT_TIMEOUT);
    });
  }, millisecs, 0);
  coro->Yield(STATE_WAITING);
}

void Scheduler::Wait(Coroutine* coro, Socket& s, int flags) {
  int events = 0;
  if (flags & WAIT_READABLE) {
    events |= UV_READABLE;
  }
  if (flags & WAIT_WRITABLE) {
    events |= UV_WRITABLE;
  }
  events |= UV_DISCONNECT;
  uv_poll_start(&s.poll_, events, [](uv_poll_t* w, int status, int events) {
    if (status > 0) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_HUP);
    } else if ((events & UV_READABLE) && (events & UV_WRITABLE)) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_RWABLE);
    } else if (events & UV_READABLE) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_READABLE);
    } else if (events & UV_WRITABLE) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_WRITABLE);
    } else if (events & UV_DISCONNECT) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_HUP);
    } else {
      //???TODO
    }
  });
  s.coro_->SetTimeout(s.GetDeadline());
  s.coro_->Yield(STATE_WAITING);
  uv_poll_stop(&s.poll_);
}

void Scheduler::Cleanup(CoroutineList& cl) {
  for (auto c : cl) {
    c->SetEvent(EVENT_CANCEL);
    c->Resume();
    c->Destroy();
  }
  cl.clear();
}

void Scheduler::PostCoroutine(Coroutine* coro, bool is_compute) {
  {
    std::lock_guard<std::mutex> l(lock_);
    if (is_compute) {
      compute_done_.push_back(coro);
    } else {
      posted_.push_back(coro);
    }
  }
  uv_async_send(&async_);
}

void Scheduler::BeginCompute(Coroutine* coro) {
#ifdef USE_UV_THREADPOOL
  uv_work_t c;
  c.data = (void*)coro;
  uv_queue_work(loop_ptr_, &c, [](uv_work_t* w) {
    ((Coroutine*)w->data)->SetEvent(EVENT_COMPUTE);
    ((Coroutine*)w->data)->Resume();
  }, [](uv_work_t* w, int status) {
    ((Coroutine*)w->data)->SetEvent(EVENT_COMPUTE_DONE);
    ((Coroutine*)w->data)->GetScheduler()->AddCoroutine((Coroutine*)w->data);
    ((Coroutine*)w->data)->GetScheduler()->outstanding_ --;
  });
#endif
  coro->Yield(STATE_COMPUTE);
}

Stack Scheduler::AllocateStack() {
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

void Scheduler::DeallocateStack(Stack& stack) {
  assert(stack.sp);
  void* vp = static_cast< char* >(stack.sp) - stack.size;
#if defined(_WIN32)
  ::VirtualFree(vp, 0, MEM_RELEASE);
#else
  munmap(vp, stack.size);
#endif
}

Scheduler* Scheduler::Get() {
  return local_sched;
}

#ifndef USE_UV_THREADPOOL
void ComputeThreads::Start() {
  for (int i = 0; i < COMPUTE_THREADS_N; i++) {
    threads_.emplace_back(std::bind(&ComputeThreads::Consume, this));
  }
}

void ComputeThreads::Stop() {
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
    i->SetEvent(EVENT_CANCEL);
    i->Resume();
    i->Destroy();
  }
  pending_.clear();
}

inline void ComputeThreads::Add(Coroutine* coro) {
  std::lock_guard<std::mutex> l(lock_);
  pending_.push_back(coro);
  cond_.notify_all();
}

void ComputeThreads::Consume() {
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
    coro->SetEvent(EVENT_COMPUTE);
    coro->Resume();
    coro->GetScheduler()->PostCoroutine(coro, true);
  }
}
#endif

}
