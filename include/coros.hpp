#pragma once

#include <uv.h>

#include <cstddef>
#include <functional>
#include <string>
#include <mutex>
#include <vector>

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

}

struct Stack {
    std::size_t size{ 0 };
    void* sp{ nullptr };
};

struct Unwind {};

enum State {
    STATE_READY = 0,
    STATE_RUNNING = 1,
    STATE_WAITING = 2,
    STATE_COMPUTE = 3,
    STATE_DONE = 4
};

enum Event {
    EVENT_CANCEL = 0,
    EVENT_READABLE = 1,
    EVENT_WRITABLE = 2,
    EVENT_TIMEOUT = 3,
    EVENT_JOIN = 4,
    EVENT_COMPUTE = 5,
    EVENT_COMPUTE_DONE = 6,
    EVENT_HUP = 7,
    EVENT_RWABLE = 8,
    EVENT_CONT = 9,
    EVENT_COND = 10,
};

enum {
    WAIT_READABLE = 0x1,
    WAIT_WRITABLE = 0x2
};

class Scheduler;
class Coroutine;

typedef std::function<void()> Callback;

class Socket {
    friend class Scheduler;

public:
    Socket();
    Socket(uv_os_sock_t s);

    void set_deadline(int timeout_secs);
    int get_deadline();
    bool listen_by_host(const std::string& host, int port, int backlog = 1024);
    bool listen_by_ip(const std::string& ip, int port, int backlog = 1024);
    void close();
    bool connect_host(const std::string& host, int port);
    bool connect_ip(const std::string& ip, int port);
    int read_some(char* buf, int len);
    int write_some(const char* buf, int len);
    uv_os_sock_t accept();
    Event wait(int flags);

protected:
    uv_os_sock_t s_;
    uv_poll_t poll_;
    int timeout_secs_{ 0 };
    Coroutine* coro_{ nullptr };
};

class Coroutine {
public:
    static Coroutine* self();

    bool create(Scheduler* sched, const Callback& fn, const Callback& exit_fn);
    void destroy();

    void resume();
    void yield(State new_state);

    void join(Coroutine* coro);
    void cancel();
    void set_event(Event new_event);

    State state() const;
    Event event() const;
    Scheduler* sched() const;
    std::size_t id() const;

    void nice();
    void wait(long millisecs);
    void begin_compute();
    void end_compute();

    void set_timeout(int seconds);
    void check_timeout();

private:
    context::fcontext_t ctx_{ nullptr };
    context::fcontext_t caller_{ nullptr };
    Stack stack_;
    Callback fn_;
    Callback exit_fn_;
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
    void wait(Coroutine* coro) {
        waiting_.push_back(coro);
        coro->yield(STATE_WAITING);
    }

    void notify_one() {
        if (waiting_.size() > 0) {
            Coroutine* coro = waiting_.back();
            waiting_.pop_back();
            coro->set_event(EVENT_COND);
        }
    }

    void notify_all() {
        for (auto it = waiting_.begin(); it != waiting_.end(); it++) {
            (*it)->set_event(EVENT_COND);
        }
        waiting_.clear();
    }

protected:
    CoroutineList waiting_;
};

class Scheduler {
public:
    static Scheduler* get();

    Scheduler(bool is_default);
    ~Scheduler();

    std::size_t next_coro_id();
    Stack allocate_stack();
    void deallocate_stack(Stack& stack);

    void add_coroutine(Coroutine* coro); // for current thread
    void post_coroutine(Coroutine* coro, bool is_compute = false); // for different thread
    void wait(Coroutine* coro, long millisecs);
    void wait(Coroutine* coro, Socket& s, int flags);
    void begin_compute(Coroutine* coro);
    void run();

    Coroutine* current() const;
    uv_loop_t* loop() const;

protected:
    void pre();
    void check();
    void async();
    void sweep();
    void run_coros();
    void cleanup(CoroutineList& cl);

protected:
    static std::size_t page_size_;
    static std::size_t min_stack_size_;
    static std::size_t max_stack_size_;
    static std::size_t default_stack_size_;
    bool is_default_;
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
};

#include "coros-inl.hpp"

}
