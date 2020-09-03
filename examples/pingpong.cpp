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

void ClientFn() {
  coros::Coroutine* c = coros::Coroutine::Self();

  coros::Socket s;
  char buf[256];
  MALOG_INFO("coro-" << c->GetId() << ", connect " << host);
  if (s.ConnectIp(host, port)) {
    MALOG_INFO("coro-" << c->GetId() << ", connected");
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
    MALOG_INFO("coro-" << c->GetId() << ", closed");
  }
}

void ServeFn(uv_os_sock_t fd) {
  /*coros::Coroutine* c = coros::Coroutine::Self();*/

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

void ExitFn(coros::Coroutine* c) {
  MALOG_INFO("coro-" << c->GetId() << ": exit");
}

void ListenerFn(coros::Schedulers* scheds) {
  coros::Socket s;
  s.ListenByIp("0.0.0.0", port);
  for (;;) {
    uv_os_sock_t s_new = s.Accept();
    if (s_new == BAD_SOCKET) {
      break;
    }
    /*coros::Coroutine* c_new = */coros::Coroutine::Create(scheds->GetNext(), std::bind(ServeFn, s_new), ExitFn);
  }
}

void usage() {
  MALOG_INFO("usage: pingpong -c 127.0.0.1 -p 9090 -n 100 -t 2");
  MALOG_INFO("       pingpong -d -p 9090 -t 2");
}

void GuardFn(coros::Schedulers* scheds) {
  coros::Coroutine* c = coros::Coroutine::Self();
  MALOG_INFO("Create " << clients << " clients");
  for (int i = 0; i < clients; i++) {
    /*coros::Coroutine* c_new = */coros::Coroutine::Create(scheds->GetNext(), ClientFn, ExitFn);
  }

  int nsecs = 0;
  for (;;) {
    c->Wait(1000);
    nsecs ++;
    std::size_t in = in_bytes;
    std::size_t out = out_bytes;
    MALOG_INFO("in=" << (in / nsecs) << " bytes per second, out=" << (out / nsecs) << " bytes per second");
  }
}

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

  coros::Scheduler sched(true);
  coros::Schedulers scheds(threads);

  if (is_server) {
    MALOG_INFO("Start pingpoing server");
    /*coros::Coroutine* c = */coros::Coroutine::Create(&sched, std::bind(ListenerFn, &scheds), ExitFn);
  } else {
    MALOG_INFO("Start guard timer");
    /*coros::Coroutine* c = */coros::Coroutine::Create(&sched, std::bind(GuardFn, &scheds), ExitFn);
  }
  sched.Run();

  return 0;
}
