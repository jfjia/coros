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

#if defined(_WIN32)
#define BAD_SOCKET (uintptr_t)(~0)
#else
#define BAD_SOCKET (-1)
#endif

namespace coros {
namespace context {

typedef void* fcontext_t;

typedef struct transfer_s {
  fcontext_t fctx;
  void* data;
} transfer_t;

extern "C" transfer_t jump_fcontext(fcontext_t const to, void* vp);
extern "C" fcontext_t make_fcontext(void* sp, size_t size, void (*fn)(transfer_t));
extern "C" transfer_t ontop_fcontext(fcontext_t const to, void* vp, transfer_t(*fn)(transfer_t));

struct Stack {
  std::size_t size{ 0 };
  void* sp{ nullptr };
};

void InitStack();
Stack AllocateStack(std::size_t stack_size);
void DeallocateStack(Stack& stack);

}

struct Unwind {};

enum State {
  STATE_READY = 0,
  STATE_RUNNING = 1,
  STATE_WAITING = 2,
  STATE_COMPUTE = 3,
  STATE_DONE = 4
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

  void Clear();

  char* Data();
  int Size();
  void RemoveConsumed(int n);

  void Commit(int n);
  void Compact();
  char* Space();
  int SpaceSize();
  char* Space(int n);

  bool Drain();
  bool Read(int min_len);

  bool WriteExactly(const char* buf, int len);

protected:
  char data_[N];
  int read_index_{ 0 };
  int write_index_{ 0 };
  Socket& s_;
};

class Coroutine {
public:
  static Coroutine* Self();

  bool Create(Scheduler* sched,
              const std::function<void()>& fn,
              const std::function<void()>& exit_fn);
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

private:
  friend class Scheduler;
  void CheckTimeout();
  static std::size_t NextId();

private:
  context::fcontext_t ctx_{ nullptr };
  context::fcontext_t caller_{ nullptr };
  context::Stack stack_;
  std::function<void()> fn_;
  std::function<void()> exit_fn_;
  Scheduler* sched_{ nullptr };
  State state_{ STATE_READY };
  Event event_;
  int timeout_secs_{ 0 };
  Coroutine* joined_{ nullptr };
  std::size_t id_{ 0 };
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

  Scheduler(bool is_default, std::size_t stack_size = 0, int compute_threads_n = 2);
  ~Scheduler();

  void AddCoroutine(Coroutine* coro); // for current thread
  void PostCoroutine(Coroutine* coro, bool is_compute = false); // for different thread
  void Wait(Coroutine* coro, long millisecs);
  void Wait(Coroutine* coro, Socket& s, int flags);
  void BeginCompute(Coroutine* coro);
  void Run();

  Coroutine* GetCurrent() const;
  uv_loop_t* GetLoop();
  std::size_t GetStackSize() const;
  std::size_t GetId() const;

  void Stop(bool graceful);

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
  std::size_t stack_size_;
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
};

template<int N>
class Schedulers {
public:
  Schedulers();

  Scheduler* GetDefault();
  Scheduler* GetNext();

  void Run();

protected:
  void Fn(int n);

protected:
  Scheduler sched_;
  std::thread threads_[N];
  Scheduler* scheds_[N];
  int rr_index_{ 0 };
};

#include "coros-inl.hpp"

}
