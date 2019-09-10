#ifndef UVPP_HPP
#define UVPP_HPP

#include <uv.h>
#include <functional>

namespace uvpp {

typedef std::function<void()> VoidCb;
typedef std::function<void(int)> IntCb;

class Loop {
public:
  ~Loop();

  int init(bool is_default = false);
  void close();

  int run();
  int runOnce();
  int runNowait();

  void stop();

  uint64_t now() const;
  void updateTime();

  bool alive() const;

  uv_loop_t* value();

private:
  uv_loop_t loop_;
  uv_loop_t* loop_ptr_;
};

template<typename T>
class Handle {
public:
  bool isActive();
  bool isClosing();
  void ref();
  void unref();
  bool hasRef();
  Loop& loop();
  void close();
  void close(const VoidCb& handler);

protected:
  T handle_;
  VoidCb closeHandler_ = []() {};
};

class Prepare : public Handle<uv_prepare_t> {
public:
  Prepare();

  int init(Loop& loop);
  int start(const VoidCb& handler);
  int stop();

private:
  VoidCb startHandler_ = []() {};
};

class Check : public Handle<uv_check_t> {
public:
  Check();

  int init(Loop& loop);
  int start(const VoidCb& handler);
  int stop();

private:
  VoidCb startHandler_ = []() {};
};

class Async : public Handle<uv_async_t> {
public:
  Async();

  int init(Loop& loop, const VoidCb& handler);
  int send();

private:
  VoidCb callbackHandler_ = []() {};
};

class Poll : public Handle<uv_poll_t> {
public:
  Poll();

  int init(Loop& loop, int fd);
  int initSocket(Loop& loop, uv_os_sock_t socket);
  int start();
  int stop();

  int disableAll();

  void onReadable(const IntCb& handler);
  int disableReadable();
  void onWritable(const IntCb& handler);
  int disableWritable();
  void onDisconnect(const IntCb& handler);
  int disableDisconnect();
  void onPrioritized(const IntCb& handler);
  int disablePrioritized();

private:
  int disable(int event);

private:
  int events_{ 0 };
  IntCb readableHandler_ = [](int ec) {};
  IntCb writableHandler_ = [](int ec) {};
  IntCb disconnectHandler_ = [](int ec) {};
  IntCb prioritizedHandler_ = [](int ec) {};
};

class Timer : public Handle<uv_timer_t> {
public:
  Timer();

  int init(Loop& loop);
  int start(const VoidCb& handler, uint64_t timeout, uint64_t repeat);
  int stop();
  int again();
  void setRepeat(uint64_t repeat);
  uint64_t getRepeat() const;

private:
  VoidCb startHandler_ = []() {};
};

#include "uvpp-inl.hpp"

}

#endif
