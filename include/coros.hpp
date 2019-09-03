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
    Socket(uv_os_sock_t s = BAD_SOCKET);

    void attach(Coroutine* coro);
    void attach(uv_os_sock_t s, Coroutine* coro);
    void set_deadline(int timeout_secs);
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
    Coroutine* coro_{ nullptr };
    int timeout_secs_{ 0 };
    std::vector<char> recv_buf_;
};

class Coroutine {
public:
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

class Scheduler {
public:
    static Scheduler* create(bool is_default = true);
    static Scheduler* get();

    ~Scheduler();

    std::size_t next_coro_id();
    Stack allocate_stack();
    void deallocate_stack(Stack& stack);

    void add_coroutine(Coroutine* coro); // for current thread
    void post_coroutine(Coroutine* coro); // for different thread
    void wait(Coroutine* coro, long millisecs);
    void wait(Coroutine* coro, Socket& s, int flags);
    void begin_compute(Coroutine* coro);
    void run();

    Coroutine* current() const;
    uv_loop_t* loop() const;

protected:
    Scheduler(bool is_default);

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
    char* recv_buf_;
};

inline Socket::Socket(uv_os_sock_t s)
    : s_(s) {
    poll_.data = (void*)this;
}

inline void Socket::attach(Coroutine* coro) {
    coro_ = coro;
    if (s_ != BAD_SOCKET) {
        uv_poll_init_socket(coro->sched()->loop(), &poll_, s_);
    }
}

inline void Socket::attach(uv_os_sock_t s, Coroutine* coro) {
    s_ = s;
    coro_ = coro;
    if (s_ != BAD_SOCKET) {
        uv_poll_init_socket(coro->sched()->loop(), &poll_, s_);
    }
}

inline void Socket::set_deadline(int timeout_secs) {
    timeout_secs_ = timeout_secs;
}

inline Event Socket::wait(int flags) {
    coro_->sched()->wait(coro_, *this, flags);
    return coro_->event();
}

inline void Coroutine::join(Coroutine* coro) {
    if (coro->state() == STATE_DONE) {
        return;
    }
    coro->joined_ = this;
    yield(STATE_WAITING);
}

inline void Coroutine::cancel() {
    set_event(EVENT_CANCEL);
}

inline void Coroutine::wait(long millisecs) {
    sched_->wait(this, millisecs);
}

inline void Coroutine::set_event(Event new_event) {
    state_ = STATE_READY;
    event_ = new_event;
}

inline State Coroutine::state() const {
    return state_;
}

inline Event Coroutine::event() const {
    return event_;
}

inline void Coroutine::resume() {
    state_ = STATE_RUNNING;
    ctx_ = context::jump_fcontext(ctx_, (void*)this).fctx;
}

inline void Coroutine::yield(State new_state) {
    state_ = new_state;
    caller_ = context::jump_fcontext(caller_, (void*)this).fctx;
    if (event_ == EVENT_CANCEL) {
        throw Unwind();
    }
}

inline Scheduler* Coroutine::sched() const {
    return sched_;
}

inline std::size_t Coroutine::id() const {
    return id_;
}

inline void Coroutine::nice() {
    yield(STATE_READY);
}

inline void Coroutine::begin_compute() {
    sched_->begin_compute(this);
}

inline void Coroutine::end_compute() {
    yield(STATE_READY);
}

inline void Coroutine::set_timeout(int seconds) {
    timeout_secs_ = seconds;
}

inline void Coroutine::check_timeout() {
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

inline Coroutine* Scheduler::current() const {
    return current_;
}

inline uv_loop_t* Scheduler::loop() const {
    return loop_ptr_;
}

inline Coroutine* coroutine_self() {
    Scheduler* sched = Scheduler::get();
    if (sched) {
        return sched->current();
    } else {
        return nullptr;
    }
}

inline bool is_in_coroutine() {
    return coroutine_self() != nullptr;
}

}
