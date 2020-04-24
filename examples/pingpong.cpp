#include "coros.hpp"
#include "malog.h"
#include <string.h>
#include <memory.h>
#include <thread>

#define NUM_WORKERS 2

std::atomic<std::size_t> in_bytes;
std::atomic<std::size_t> out_bytes;

bool is_server = true;
std::string host;

class Conn {
public:
  bool Start(coros::Scheduler* sched) {
    if (!coro_.Create(sched, std::bind(&Conn::Fn1, this), std::bind(&Conn::ExitFn, this))) {
      return false;
    }
    MALOG_INFO("coro=" << coro_.GetId() << ", created");
    return true;
  }

  bool Start(coros::Scheduler* sched, uv_os_sock_t fd) {
    if (!coro_.Create(sched, std::bind(&Conn::Fn, this, fd), std::bind(&Conn::ExitFn, this))) {
      return false;
    }
    MALOG_INFO("coro=" << coro_.GetId() << ", created");
    return true;
  }

  void Fn1() {
    coros::Socket s;
    char buf[256];
    MALOG_INFO("coro=" << coro_.GetId() << ", connect " << host);
    if (s.ConnectIp(host, 9090)) {
      MALOG_INFO("coro=" << coro_.GetId() << ", connected");
      s.WriteExactly(buf, 256);
      out_bytes += 256;
      for (;;) {
        int len = s.ReadSome(buf, 256);
        if (len <= 0) {
          break;
        }
        in_bytes += len;
        len = s.WriteExactly(buf, len);
        out_bytes += len;
        if (len <= 0) {
          break;
        }
      }
      s.Close();
    }
  }

  void Fn(uv_os_sock_t fd) {
    coros::Socket s(fd);
    char buf[256];
    for (;;) {
      int len = s.ReadSome(buf, 256);
      if (len <= 0) {
        break;
      }
      in_bytes += len;
      len = s.WriteExactly(buf, len);
      out_bytes += len;
      if (len <= 0) {
        break;
      }
    }
    s.Close();
  }

  void ExitFn() {
    delete this;
  }

private:
  coros::Coroutine coro_;
};

class Listener {
public:
  bool Start(coros::Schedulers<NUM_WORKERS>* scheds) {
    scheds_ = scheds;
    if (!coro_.Create(scheds->GetDefault(), std::bind(&Listener::Fn, this), std::bind(&Listener::ExitFn, this))) {
      return false;
    }
    return true;
  }

  void Fn() {
    coros::Socket s;
    s.ListenByIp("0.0.0.0", 9090);
    for (;;) {
      uv_os_sock_t s_new = s.Accept();
      if (s_new == BAD_SOCKET) {
        break;
      }
      Conn* c = new Conn();
      c->Start(scheds_->GetNext(), s_new);
    }
  }

  void ExitFn() {
  }

protected:
  coros::Coroutine coro_;
  coros::Schedulers<NUM_WORKERS>* scheds_;
};

void usage() {
  MALOG_INFO("usage: pingpong -c 127.0.0.1 -p 9090");
  MALOG_INFO("       pingpong -d");
}

class Guard {
public:
  bool Start(coros::Schedulers<NUM_WORKERS>* scheds) {
    scheds_ = scheds;
    if (!coro_.Create(scheds->GetDefault(), std::bind(&Guard::Fn, this), std::bind(&Guard::ExitFn, this))) {
      return false;
    }
    return true;
  }

  void Fn() {
    MALOG_INFO("Create 100 clients");
    for (int i = 0; i < 100; i++) {
      Conn* c = new Conn();
      c->Start(scheds_->GetNext());
    }

    for (;;) {
      coro_.Wait(1000);
      std::size_t in = in_bytes;
      std::size_t out = out_bytes;
      MALOG_INFO("in=" << in << ", out=" << out);
    }
  }

  void ExitFn() {
  }

protected:
  coros::Coroutine coro_;
  coros::Schedulers<NUM_WORKERS>* scheds_;
};

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-d") {
      is_server = true;
    } else if (arg == "-c") {
      is_server = false;
      if (argc > i + 1) {
        i ++;
        host = argv[i];
      } else {
        usage();
        exit(1);
      }
    }
  }

  coros::Schedulers<NUM_WORKERS> scheds;
  scheds.Start();
  if (is_server) {
    MALOG_INFO("Start pingpoing server");
    Listener l;
    l.Start(&scheds);
    scheds.Run();
  } else {
    MALOG_INFO("Start guard timer");
    Guard* g = new Guard();
    g->Start(&scheds);
    scheds.Run();
  }
  return 0;
}
