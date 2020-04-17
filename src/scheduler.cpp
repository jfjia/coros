#include "coros.hpp"
#include <cassert>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace coros {

static const int SWEEP_INTERVAL = 1000;
static const int RECV_BUFFER_SIZE = 64 * 1024;

thread_local Scheduler* local_sched = nullptr;

class ComputeThreads {
public:
  void Start(int compute_threads_n);
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

Scheduler::Scheduler(bool is_default, std::size_t stack_size, int compute_threads_n)
  : is_default_(is_default), stack_size_(stack_size) {
  if (is_default) {
    context::InitStack();
    loop_ptr_ = uv_default_loop();
    compute_threads.Start(compute_threads_n);
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
  local_sched = nullptr;
  uv_loop_close(loop_ptr_);
  if (is_default_) {
    compute_threads.Stop();
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
        compute_threads.Add(c);
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
      (reinterpret_cast<Coroutine*>(handle->data))->Wakeup(EVENT_TIMEOUT);
    });
  }, millisecs, 0);
  coro->Suspend(STATE_WAITING);
}

void Scheduler::Cleanup(CoroutineList& cl) {
  for (auto c : cl) {
    c->Wakeup(EVENT_CANCEL);
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

Scheduler* Scheduler::Get() {
  return local_sched;
}

void ComputeThreads::Start(int compute_threads_n) {
  for (int i = 0; i < compute_threads_n; i++) {
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
    i->Wakeup(EVENT_CANCEL);
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
    coro->Resume();
    coro->GetScheduler()->PostCoroutine(coro, true);
  }
}

}
