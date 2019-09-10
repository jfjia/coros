inline Loop::~Loop() {
  close();
}

inline void Loop::close() {
  if (loop_ptr_ != nullptr) {
    uv_loop_close(loop_ptr_);
    loop_ptr_ = nullptr;
  }
}

inline int Loop::init(bool is_default) {
  if (is_default) {
    loop_ptr_ = uv_default_loop();
    if (loop_ptr_ == nullptr) {
      return (ENOMEM);
    }
  } else {
    auto status = uv_loop_init(loop_ptr_);
    if (status != 0) {
      return (status);
    }
    loop_ptr_ = &loop_;
  }
  loop_ptr_->data = this;
  return 0;
}

inline int Loop::run() {
  return uv_run(loop_ptr_, UV_RUN_DEFAULT);
}

inline int Loop::runOnce() {
  return uv_run(loop_ptr_, UV_RUN_ONCE);
}

inline int Loop::runNowait() {
  return uv_run(loop_ptr_, UV_RUN_NOWAIT);
}

inline void Loop::stop() {
  uv_stop(loop_ptr_);
}

inline uint64_t Loop::now() const {
  return uv_now(loop_ptr_);
}

inline void Loop::updateTime() {
  uv_update_time(loop_ptr_);
}

inline bool Loop::alive() const {
  return uv_loop_alive(loop_ptr_);
}

inline uv_loop_t* Loop::value() {
  return loop_ptr_;
}

template<typename T>
inline bool Handle<T>::isActive() {
  return uv_is_active(reinterpret_cast<uv_handle_t*>(&handle_));
}

template<typename T>
inline bool Handle<T>::isClosing() {
  return uv_is_closing(reinterpret_cast<uv_handle_t*>(&handle_));
}

template<typename T>
inline void Handle<T>::ref() {
  uv_ref(reinterpret_cast<uv_handle_t*>(&handle_));
}

template<typename T>
inline void Handle<T>::unref() {
  uv_unref(reinterpret_cast<uv_handle_t*>(&handle_));
}

template<typename T>
inline bool Handle<T>::hasRef() {
  return uv_has_ref(reinterpret_cast<uv_handle_t*>(&handle_));
}

template<typename T>
inline Loop& Handle<T>::loop() {
  return *reinterpret_cast<Loop*>(
           reinterpret_cast<uv_handle_t*>(&handle_)->loop->data);
}

template<typename T>
inline void Handle<T>::close() {
  uv_close(reinterpret_cast<uv_handle_t*>(&handle_), NULL);
}

template<typename T>
inline void Handle<T>::close(const VoidCb& handler) {
  closeHandler_ = handler;
  uv_close(reinterpret_cast<uv_handle_t*>(&handle_), [](uv_handle_t* h) {
    auto& handle = *reinterpret_cast<Handle<T> *>(h->data);
    handle.closeHandler_();
  });
}

inline Prepare::Prepare() {
  handle_.data = this;
}

inline int Prepare::init(Loop& loop) {
  return uv_prepare_init(loop.value(), &handle_);
}

inline int Prepare::start(const VoidCb& handler) {
  startHandler_ = handler;
  return uv_prepare_start(&handle_, [](uv_prepare_t* handle) {
    auto& prepare = *reinterpret_cast<Prepare*>(handle->data);
    prepare.startHandler_();
  });
}

inline int Prepare::stop() {
  return uv_prepare_stop(&handle_);
}

inline Check::Check() {
  handle_.data = this;
}

inline int Check::init(Loop& loop) {
  return uv_check_init(loop.value(), &handle_);
}

inline int Check::start(const VoidCb& handler) {
  startHandler_ = handler;
  return uv_check_start(&handle_, [](uv_check_t* handle) {
    auto& check = *reinterpret_cast<Check*>(handle->data);
    check.startHandler_();
  });
}

inline int Check::stop() {
  return uv_check_stop(&handle_);
}

inline Async::Async() {
  handle_.data = this;
}

inline int Async::init(Loop& loop, const VoidCb& handler) {
  callbackHandler_ = handler;
  return uv_async_init(loop.value(), &handle_, [](uv_async_t* a) {
    auto& async = *reinterpret_cast<Async*>(a->data);
    async.callbackHandler_();
  });
}

inline int Async::send() {
  return uv_async_send(&handle_);
}

inline Poll::Poll() {
  handle_.data = this;
}

inline int Poll::init(Loop& loop, int fd) {
  return uv_poll_init(loop.value(), &handle_, fd);
}

inline int Poll::initSocket(Loop& loop, uv_os_sock_t socket) {
  return uv_poll_init_socket(loop.value(), &handle_, socket);
}

inline int Poll::start() {
  return uv_poll_start(&handle_, events_, [](uv_poll_t* handle, int status, int events) {
    auto& poll = *reinterpret_cast<Poll*>(handle->data);
    if (events & UV_READABLE) {
      poll.readableHandler_(status);
    }
    if (events & UV_WRITABLE) {
      poll.writableHandler_(status);
    }
    if (events & UV_DISCONNECT) {
      poll.disconnectHandler_(status);
    }
    if (events & UV_PRIORITIZED) {
      poll.prioritizedHandler_(status);
    }
  });
}

inline int Poll::stop() {
  return uv_poll_stop(&handle_);
}

inline int Poll::disableAll() {
  events_ = 0;
  return stop();
}

inline void Poll::onReadable(const IntCb& handler) {
  events_ += UV_READABLE;
  readableHandler_ = handler;
}

inline int Poll::disableReadable() {
  return disable(UV_READABLE);
}

inline void Poll::onWritable(const IntCb& handler) {
  events_ += UV_WRITABLE;
  writableHandler_ = handler;
}

inline int Poll::disableWritable() {
  return disable(UV_WRITABLE);
}

inline void Poll::onDisconnect(const IntCb& handler) {
  events_ += UV_DISCONNECT;
  disconnectHandler_ = handler;
}

inline int Poll::disableDisconnect() {
  return disable(UV_DISCONNECT);
}

inline void Poll::onPrioritized(const IntCb& handler) {
  events_ += UV_PRIORITIZED;
  prioritizedHandler_ = handler;
}

inline int Poll::disablePrioritized() {
  return disable(UV_PRIORITIZED);
}

inline int Poll::disable(int event) {
  events_ &= ~event;
  return start();
}

inline Timer::Timer() {
  handle_.data = this;
}

inline int Timer::init(Loop& loop) {
  return uv_timer_init(loop.value(), &handle_);
}

inline int Timer::start(const VoidCb& handler, uint64_t timeout, uint64_t repeat) {
  startHandler_ = handler;
  return uv_timer_start(&handle_, [](uv_timer_t* handle) {
    auto& timer = *reinterpret_cast<Timer*>(handle->data);
    timer.startHandler_();
  }, timeout, repeat);
}

inline int Timer::stop() {
  return uv_timer_stop(&handle_);
}

inline int Timer::again() {
  return uv_timer_again(&handle_);
}

inline void Timer::setRepeat(uint64_t repeat) {
  uv_timer_set_repeat(&handle_, repeat);
}

inline uint64_t Timer::getRepeat() const {
  return uv_timer_get_repeat(&handle_);
}
