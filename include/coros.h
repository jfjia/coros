#ifndef COROS_H
#define COROS_H

#pragma once

#include <uv.h>

#include <cstddef>
#include <cstring>
#include <cassert>

#include <atomic>
#include <functional>
#include <string>
#include <mutex>
#include <vector>
#include <thread>
#include <condition_variable>

#include <boost/context/detail/fcontext.hpp>
#include <boost/context/fixedsize_stack.hpp>

#if defined(_WIN32)
#define BAD_SOCKET (uintptr_t)(~0)
#else
#define BAD_SOCKET (-1)
#endif

namespace coros {

struct Unwind {};

enum State {
  STATE_READY = 0,
  STATE_RUNNING = 1,
  STATE_WAITING = 2,
  STATE_COMPUTE = 3,
  STATE_DONE = 4,
};

enum Event {
  EVENT_WAKEUP = 0,
  EVENT_CANCEL = 1,
  EVENT_READABLE = 2,
  EVENT_WRITABLE = 3,
  EVENT_TIMEOUT = 4,
  EVENT_JOIN = 5,
  EVENT_COND = 6,
  EVENT_POLLERR = 7,
  EVENT_DISCONNECT = 8,
};

class Scheduler;
class Coroutine;
class Condition;

class Socket {
  friend class Scheduler;

public:
  Socket(uv_os_sock_t s = BAD_SOCKET);

  void Close();

  void SetDeadline(int timeout_secs);
  int GetDeadline();

  bool ListenByHost(const std::string& host, int port, int backlog = 1024);
  bool ListenByIp(const std::string& ip, int port, int backlog = 1024);
  uv_os_sock_t Accept();

  bool ConnectHost(const std::string& host, int port);
  bool ConnectIp(const std::string& ip, int port);

  int ReadSome(char* buf, int len);
  int ReadExactly(char* buf, int len);
  int ReadAtLeast(char* buf, int len, int min_len);
  int WriteSome(const char* buf, int len);
  int WriteExactly(const char* buf, int len);

  Event WaitReadable(Condition* cond = nullptr);
  Event WaitWritable();

protected:
  uv_os_sock_t s_;
  uv_poll_t poll_;
  int timeout_secs_{ 0 };
  Coroutine* coro_{ nullptr };
};

template<int N>
class Buffer {
public:
  Buffer(Socket& s);

  int EnsureData(int n);
  char* Data();
  int Size();
  void Skip(int n);

  int EnsureSpace(int n);
  char* Space();
  int SpaceSize();
  void Commit(int n);

  void Clear();
  int Flush();

  void Compact();

protected:
  char data_[N];
  int read_index_{ 0 };
  int write_index_{ 0 };
  Socket& s_;
};

class Coroutine {
public:
  static Coroutine* Self();

  static Coroutine* Create(Scheduler* sched,
                           const std::function<void()>& fn,
                           const std::function<void(Coroutine*)>& exit_fn,
                           std::size_t stack_size);
  void Destroy();

  void Resume();
  void Suspend(State new_state);

  void Join(Coroutine* coro);
  void Cancel();
  void Wakeup(Event new_event = EVENT_WAKEUP);

  State GetState() const;
  Event GetEvent() const;
  Scheduler* GetScheduler() const;
  std::size_t GetId() const;

  void Nice();
  void Wait(long millisecs);
  void BeginCompute();
  void EndCompute();

  void SetTimeout(int seconds);
  bool CheckBuget();

private:
  friend class Scheduler;
  void CheckTimeout();
  static std::size_t NextId();

private:
  boost::context::detail::fcontext_t ctx_{ nullptr };
  boost::context::detail::fcontext_t caller_{ nullptr };
  boost::context::fixedsize_stack stack_alloc_;
  boost::context::stack_context stack_;
  std::function<void()> fn_;
  std::function<void(Coroutine*)> exit_fn_;
  Scheduler* sched_{ nullptr };
  State state_{ STATE_READY };
  Event event_;
  int timeout_secs_{ 0 };
  Coroutine* joined_{ nullptr };
  std::size_t id_{ 0 };
  int buget_{ 0 };
};

typedef std::vector<Coroutine* > CoroutineList;

class Condition {
public:
  void Wait(Coroutine* coro);

  void NotifyOne();
  void NotifyAll();

protected:
  CoroutineList waiting_;
};

class Scheduler {
public:
  static Scheduler* Get();

  Scheduler(bool is_default, int compute_threads_n = 2);
  ~Scheduler();

  void AddCoroutine(Coroutine* coro); // for current thread
  void PostCoroutine(Coroutine* coro, bool is_compute = false); // for different thread
  void Wait(Coroutine* coro, long millisecs);
  void Wait(Coroutine* coro, Socket& s, int flags);
  void BeginCompute(Coroutine* coro);
  void Run();

  Coroutine* GetCurrent() const;
  uv_loop_t* GetLoop();
  std::size_t GetId() const;

  void Stop(bool graceful);
  void SetScheduleParams(int tight_loop, int coro_buget);

protected:
  void Pre();
  void Check();
  void Async();
  void Sweep();
  void RunCoros();
  void Cleanup(CoroutineList& cl);
  static std::size_t NextId();

protected:
  bool is_default_;
  std::size_t id_{ 0 };
  uv_loop_t loop_;
  uv_loop_t* loop_ptr_{ nullptr };
  uv_prepare_t pre_;
  uv_check_t check_;
  uv_async_t async_;
  uv_timer_t sweep_timer_;
  Coroutine* current_{ nullptr };
  CoroutineList ready_;
  CoroutineList waiting_;
  std::mutex lock_;
  int outstanding_{ 0 };
  CoroutineList posted_;
  CoroutineList compute_done_;
  std::atomic<bool> shutdown_{ false };
  std::atomic<bool> graceful_{ false };
  int tight_loop_{ 512 };
  int coro_buget_{ 32 };
};

class Schedulers {
public:
  Schedulers(int N);

  Scheduler* GetNext();

  void Stop();

protected:
  void Fn(int n);

protected:
  int N_;
  std::vector<std::thread> threads_;
  std::vector<Scheduler*> scheds_;
  int rr_index_{ 0 };
  std::mutex lock_;
  std::condition_variable cond_;
  int created_{ 0 };
};

inline Socket::Socket(uv_os_sock_t s)
  : s_(s) {
  poll_.data = this;
  coro_ = Coroutine::Self();
  if (s != BAD_SOCKET) {
    uv_poll_init_socket(Scheduler::Get()->GetLoop(), &poll_, s_);
  }
}

inline void Socket::SetDeadline(int timeout_secs) {
  timeout_secs_ = timeout_secs;
}

inline int Socket::GetDeadline() {
  return timeout_secs_;
}

inline int Socket::ReadExactly(char* buf, int len) {
  return ReadAtLeast(buf, len, len);
}

template<int N>
inline Buffer<N>::Buffer(Socket& s) : s_(s) {
}

template<int N>
inline void Buffer<N>::Clear() {
  read_index_ = write_index_ = 0;
}

template<int N>
inline char* Buffer<N>::Data() {
  return &data_[read_index_];
}

template<int N>
inline int Buffer<N>::Size() {
  return write_index_ - read_index_;
}

template<int N>
inline void Buffer<N>::Skip(int n) {
  if (n >= Size()) {
    Clear();
  } else {
    read_index_ += n;
  }
}

template<int N>
inline void Buffer<N>::Commit(int n) {
  if ((Size() + n) <= N) {
    write_index_ += n;
  }
}

template<int N>
inline void Buffer<N>::Compact() {
  int size = Size();
  memmove(&data_[0], &data_[read_index_], size);
  read_index_ = 0;
  write_index_ = size;
}

template<int N>
inline char* Buffer<N>::Space() {
  return &data_[write_index_];
}

template<int N>
inline int Buffer<N>::EnsureSpace(int n) {
  if (SpaceSize() >= n) {
    return n;
  }
  if ((N - Size()) >= n) {
    Compact();
    return n;
  }
  if (N < n) {
    return -1;
  }
  int rc = s_.WriteExactly(Data(), Size());
  if (rc != Size()) {
    return rc;
  }
  Clear();
  return n;
}

template<int N>
inline int Buffer<N>::SpaceSize() {
  return N - write_index_;
}

template<int N>
inline int Buffer<N>::Flush() {
  int size = Size();
  int rc = s_.WriteExactly(Data(), size);
  if (rc != size) {
    return rc;
  }
  Clear();
  return size;
}

template<int N>
inline int Buffer<N>::EnsureData(int n) {
  assert(n <= N);
  if (Size() >= n) {
    return n;
  }
  if ((N - read_index_) < n) {
    Compact();
  }
  int rc = s_.ReadAtLeast(Space(), SpaceSize(), n - Size());
  if (rc <= 0) {
    return rc;
  }
  Commit(n);
  return n;
}

inline Coroutine* Coroutine::Self() {
  Scheduler* sched = Scheduler::Get();
  if (sched) {
    return sched->GetCurrent();
  } else {
    return nullptr;
  }
}

inline void Coroutine::Join(Coroutine* coro) {
  if (coro->GetState() == STATE_DONE) {
    return;
  }
  coro->joined_ = this;
  Suspend(STATE_WAITING);
}

inline void Coroutine::Cancel() {
  Wakeup(EVENT_CANCEL);
}

inline void Coroutine::Wait(long millisecs) {
  sched_->Wait(this, millisecs);
}

inline void Coroutine::Wakeup(Event new_event) {
  state_ = STATE_READY;
  event_ = new_event;
}

inline State Coroutine::GetState() const {
  return state_;
}

inline Event Coroutine::GetEvent() const {
  return event_;
}

inline bool Coroutine::CheckBuget() {
  buget_ --;
  return (buget_ >= 0);
}

inline void Coroutine::Resume() {
  state_ = STATE_RUNNING;
  ctx_ = boost::context::detail::jump_fcontext(ctx_, (void*)this).fctx;
}

inline void Coroutine::Suspend(State new_state) {
  state_ = new_state;
  caller_ = boost::context::detail::jump_fcontext(caller_, (void*)this).fctx;
  if (event_ == EVENT_CANCEL) {
    throw Unwind();
  }
}

inline Scheduler* Coroutine::GetScheduler() const {
  return sched_;
}

inline std::size_t Coroutine::GetId() const {
  return id_;
}

inline void Coroutine::Nice() {
  Suspend(STATE_READY);
}

inline void Coroutine::BeginCompute() {
  sched_->BeginCompute(this);
}

inline void Coroutine::EndCompute() {
  Suspend(STATE_READY);
}

inline void Coroutine::SetTimeout(int seconds) {
  timeout_secs_ = seconds;
}

inline void Coroutine::CheckTimeout() {
  if (state_ == STATE_WAITING) {
    if (timeout_secs_ > 0) {
      timeout_secs_ --;
      if (timeout_secs_ == 0) {
        state_ = STATE_READY;
        event_ = EVENT_TIMEOUT;
      }
    }
  }
}

inline void Condition::Wait(Coroutine* coro) {
  waiting_.push_back(coro);
  coro->Suspend(STATE_WAITING);
}

inline void Condition::NotifyOne() {
  if (waiting_.size() > 0) {
    Coroutine* coro = waiting_.back();
    waiting_.pop_back();
    coro->Wakeup(EVENT_COND);
  }
}

inline void Condition::NotifyAll() {
  for (auto it = waiting_.begin(); it != waiting_.end(); it++) {
    (*it)->Wakeup(EVENT_COND);
  }
  waiting_.clear();
}

inline Coroutine* Scheduler::GetCurrent() const {
  return current_;
}

inline uv_loop_t* Scheduler::GetLoop() {
  return loop_ptr_;
}

inline std::size_t Scheduler::GetId() const {
  return id_;
}

inline void Scheduler::SetScheduleParams(int tight_loop, int coro_buget) {
  tight_loop_ = tight_loop;
  coro_buget_ = coro_buget;
}

inline void Scheduler::BeginCompute(Coroutine* coro) {
  coro->Suspend(STATE_COMPUTE);
}

inline Scheduler* Schedulers::GetNext() {
  return scheds_[(rr_index_ ++) % N_];
}

} // coros

#endif // COROS_H
