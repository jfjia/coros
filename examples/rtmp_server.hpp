#pragma once

#include <cstdint>
#include "coros.hpp"

class Server {
public:
  bool Start();

  void Fn();
  void ExitFn();

protected:
  coros::Coroutine coro_;
};
