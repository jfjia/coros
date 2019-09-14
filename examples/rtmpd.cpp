#include <cstdint>
#include "coros.hpp"
#include "malog.h"

struct Challenge {
  uint32_t time;
  uint8_t version[4];
  uint8_t randomBytes[1528];
};

class Conn {
public:
  bool Start(uv_os_sock_t fd) {
    if (!coro_.Create(coros::Scheduler::Get(), std::bind(&Conn::Fn, this, fd), std::bind(&Conn::ExitFn, this))) {
      return false;
    }
    return true;
  }

  void Fn(uv_os_sock_t fd) {
    coros::Socket s(fd);
    s.SetDeadline(30);
    do {
      if (!HandshakeC0(s)) {
        break;
      }
      if (!HandshakeC1(s)) {
        break;
      }
      if (!HandshakeC2(s)) {
        break;
      }
      MALOG_INFO("handshake done");
    } while (0);

    s.Close();
  }

  void ExitFn() {
    MALOG_INFO("coro[" << coro_.GetId() << "] exit");
    delete this;
  }

protected:
  bool HandshakeC0(coros::Socket& s) {
    uint8_t c0;
    if (s.ReadExactly((char*)&c0, 1) != 1) {
      return false;
    }
    MALOG_INFO("handshake, c0=" << std::to_string(c0));
    if (s.WriteExactly((const char*)&c0, 1) != 1) {
      return false;
    }
    return true;
  }

  bool HandshakeC1(coros::Socket& s) {
    Challenge c1;
    if (s.ReadExactly((char*)&c1, sizeof(c1)) != sizeof(c1)) {
      return false;
    }
    MALOG_INFO("handshake, c1 time: " << c1.time <<
               ", version: " << static_cast<uint32_t>(c1.version[0]) << "." <<
               static_cast<uint32_t>(c1.version[1]) << "." <<
               static_cast<uint32_t>(c1.version[2]) << "." <<
               static_cast<uint32_t>(c1.version[3]));
    Challenge s1;
    s1.time = 0;
    s1.version[0] = 2;
    s1.version[1] = 0;
    s1.version[2] = 0;
    s1.version[3] = 0;
    if (s.WriteExactly((const char*)&s1, sizeof(s1)) != sizeof(s1)) {
      return false;
    }
    if (s.WriteExactly((const char*)&c1, sizeof(c1)) != sizeof(c1)) {
      return false;
    }
    return true;
  }

  bool HandshakeC2(coros::Socket& s) {
    Challenge c2;
    if (s.ReadExactly((char*)&c2, sizeof(c2)) != sizeof(c2)) {
      return false;
    }
    MALOG_INFO("handshake, c2 time: " << c2.time <<
               ", version: " << static_cast<uint32_t>(c2.version[0]) << "." <<
               static_cast<uint32_t>(c2.version[1]) << "." <<
               static_cast<uint32_t>(c2.version[2]) << "." <<
               static_cast<uint32_t>(c2.version[3]));
    return true;
  }

private:
  coros::Coroutine coro_;
};

class Listener {
public:
  bool Start() {
    if (!coro_.Create(coros::Scheduler::Get(), std::bind(&Listener::Fn, this), std::bind(&Listener::ExitFn, this))) {
      return false;
    }
    return true;
  }

  void Fn() {
    coros::Socket s;
    s.ListenByIp("0.0.0.0", 9090);
    for (;;) {
      uv_os_sock_t s_new = s.Accept();
      MALOG_INFO("coro[" << coro_.GetId() << ": accept new " << s_new);
      if (s_new == BAD_SOCKET) {
        break;
      }
      Conn* c = new Conn();
      c->Start(s_new);
    }
  }

  void ExitFn() {
  }

protected:
  coros::Coroutine coro_;
};

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);
  coros::Scheduler sched(true);
  Listener l;
  l.Start();
  sched.Run();
  return 0;
}
