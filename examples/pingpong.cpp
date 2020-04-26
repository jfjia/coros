#include "coros.hpp"
#include "malog.h"
#include <string.h>
#include <memory.h>
#include <thread>

std::atomic<std::size_t> in_bytes;
std::atomic<std::size_t> out_bytes;

bool is_server = true;
std::string host = "127.0.0.1";
int port = 9090;
int clients = 100;
int threads = 2;

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
    if (s.ConnectIp(host, port)) {
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
        coro_.Nice();
      }
      s.Close();
      MALOG_INFO("coro=" << coro_.GetId() << ", closed");
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
      coro_.Nice();
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
  bool Start(coros::Schedulers* scheds) {
    scheds_ = scheds;
    if (!coro_.Create(scheds->GetDefault(), std::bind(&Listener::Fn, this), std::bind(&Listener::ExitFn, this))) {
      return false;
    }
    return true;
  }

  void Fn() {
    coros::Socket s;
    s.ListenByIp("0.0.0.0", port);
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
    delete this;
  }

protected:
  coros::Coroutine coro_;
  coros::Schedulers* scheds_;
};

void usage() {
  MALOG_INFO("usage: pingpong -c 127.0.0.1 -p 9090 -n 100 -t 2");
  MALOG_INFO("       pingpong -d -p 9090 -t 2");
}

class Guard {
public:
  bool Start(coros::Schedulers* scheds) {
    scheds_ = scheds;
    if (!coro_.Create(scheds->GetDefault(), std::bind(&Guard::Fn, this), std::bind(&Guard::ExitFn, this))) {
      return false;
    }
    return true;
  }

  void Fn() {
    MALOG_INFO("Create " << clients << " clients");
    for (int i = 0; i < clients; i++) {
      Conn* c = new Conn();
      c->Start(scheds_->GetNext());
    }

    int nsecs = 0;
    for (;;) {
      coro_.Wait(1000);
      nsecs ++;
      std::size_t in = in_bytes;
      std::size_t out = out_bytes;
      MALOG_INFO("in=" << (in / nsecs) << " bytes per second, out=" << (out / nsecs) << " bytes per second");
    }
  }

  void ExitFn() {
    delete this;
  }

protected:
  coros::Coroutine coro_;
  coros::Schedulers* scheds_;
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
    } else if (arg == "-p") {
      if (argc > i + 1) {
        i ++;
        port = atoi(argv[i]);
      } else {
        usage();
        exit(1);
      }
    } else if (arg == "-n") {
      if (argc > i + 1) {
        i ++;
        clients = atoi(argv[i]);
      } else {
        usage();
        exit(1);
      }
    } else if (arg == "-t") {
      if (argc > i + 1) {
        i ++;
        threads = atoi(argv[i]);
      } else {
        usage();
        exit(1);
      }
    }
  }

  coros::Schedulers scheds(threads);

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
