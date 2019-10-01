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

inline int Socket::ReadExactly(char* buf, int len) {
  return ReadAtLeast(buf, len, len);
}

inline Event Socket::Wait(int flags) {
  coro_->GetScheduler()->Wait(coro_, *this, flags);
  return coro_->GetEvent();
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
inline void Buffer<N>::RemoveConsumed(int n) {
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
inline int Buffer<N>::SpaceSize() {
  return N - write_index_;
}

template<int N>
inline int Buffer<N>::Drain(Socket& s) {
  return s.WriteExactly(Data(), Size());
}

template<int N>
inline int Buffer<N>::Read(Socket& s, int min_len) {
  if (min_len > N) {
    return -1;
  }
  if (Size() >= min_len) {
    return Size();
  }
  if ((N - read_index_) < min_len) {
    Compact();
  }
  int n = s.ReadAtLeast(Space(), SpaceSize(), min_len - Size());
  if (n <= 0) {
    return n;
  }
  Commit(n);
  return Size();
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

inline void Coroutine::Suspend(State new_state) {
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
    coro->SetEvent(EVENT_COND);
  }
}

inline void Condition::NotifyAll() {
  for (auto it = waiting_.begin(); it != waiting_.end(); it++) {
    (*it)->SetEvent(EVENT_COND);
  }
  waiting_.clear();
}

inline Coroutine* Scheduler::GetCurrent() const {
  return current_;
}

inline uv_loop_t* Scheduler::GetLoop() {
  return loop_ptr_;
}

inline context::Stack Scheduler::AllocateStack() {
  return context::AllocateStack(stack_size_);
}

inline void Scheduler::DeallocateStack(context::Stack& stack) {
  context::DeallocateStack(stack);
}

inline void Scheduler::BeginCompute(Coroutine* coro) {
  coro->Suspend(STATE_COMPUTE);
}
