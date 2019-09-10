inline Socket::Socket() {
  poll_.data = this;
  coro_ = Coroutine::Self();
}

inline Socket::Socket(uv_os_sock_t s)
  : s_(s) {
  poll_.data = this;
  coro_ = Coroutine::Self();
  uv_poll_init_socket(Scheduler::Get()->GetLoop(), &poll_, s_);
}

inline void Socket::SetDeadline(int timeout_secs) {
  timeout_secs_ = timeout_secs;
}

inline int Socket::GetDeadline() {
  return timeout_secs_;
}

inline Event Socket::Wait(int flags) {
  coro_->GetScheduler()->Wait(coro_, *this, flags);
  return coro_->GetEvent();
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
  Yield(STATE_WAITING);
}

inline void Coroutine::Cancel() {
  SetEvent(EVENT_CANCEL);
}

inline void Coroutine::Wait(long millisecs) {
  sched_->Wait(this, millisecs);
}

inline void Coroutine::SetEvent(Event new_event) {
  state_ = STATE_READY;
  event_ = new_event;
}

inline State Coroutine::GetState() const {
  return state_;
}

inline Event Coroutine::GetEvent() const {
  return event_;
}

inline void Coroutine::Resume() {
  state_ = STATE_RUNNING;
  ctx_ = context::jump_fcontext(ctx_, (void*)this).fctx;
}

inline void Coroutine::Yield(State new_state) {
  state_ = new_state;
  caller_ = context::jump_fcontext(caller_, (void*)this).fctx;
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
  Yield(STATE_READY);
}

inline void Coroutine::BeginCompute() {
  sched_->BeginCompute(this);
}

inline void Coroutine::EndCompute() {
  Yield(STATE_READY);
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

inline Coroutine* Scheduler::GetCurrent() const {
  return current_;
}

inline uv_loop_t* Scheduler::GetLoop() {
  return loop_.value();
}

