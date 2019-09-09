inline Socket::Socket() {
    poll_.data = this;
    coro_ = Coroutine::self();
}

inline Socket::Socket(uv_os_sock_t s)
    : s_(s) {
    poll_.data = this;
    coro_ = Coroutine::self();
    uv_poll_init_socket(Scheduler::get()->loop(), &poll_, s_);
}

inline void Socket::set_deadline(int timeout_secs) {
    timeout_secs_ = timeout_secs;
}

inline Event Socket::wait(int flags) {
    coro_->sched()->wait(coro_, *this, flags);
    return coro_->event();
}

inline Coroutine* Coroutine::self() {
    Scheduler* sched = Scheduler::get();
    if (sched) {
        return sched->current();
    } else {
        return nullptr;
    }
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

