#include "coros.hpp"
#include <cassert>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>

namespace coros {
inline uv_os_sock_t SetNoSigPipe(uv_os_sock_t s) {
#ifdef SO_NOSIGPIPE
  if (s != BAD_SOCKET) {
    int no_sigpipe = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(int));
  }
#endif
  return s;
}

inline uv_os_sock_t SetNonblocking(uv_os_sock_t s) {
  if (s != BAD_SOCKET) {
#ifdef _WIN32
//TODO: libuv will set non-blocking
    unsigned long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  }
  return s;
}

inline uv_os_sock_t CreateSocket(int domain, int type, int protocol) {
  int flags = 0;
#if defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
  flags = SOCK_CLOEXEC | SOCK_NONBLOCK;
#endif
  return SetNonblocking(SetNoSigPipe(socket(domain, type | flags, protocol)));
}

inline uv_os_sock_t CreateListenSocket(int domain, int type, int protocol) {
  uv_os_sock_t s = CreateSocket(domain, type, protocol);
  if (s != BAD_SOCKET) {
#ifdef SO_REUSEPORT
    {
      int optval = 1;
      setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    }
#endif

    {
      int enabled = 1;
      setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&enabled, sizeof(enabled));
    }

#ifdef IPV6_V6ONLY
    int disabled = 0;
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&disabled, sizeof(disabled));
#endif
  }
  return s;
}

inline uv_os_sock_t CloseSocket(uv_os_sock_t s) {
  if (s != BAD_SOCKET) {
#if defined(_WIN32)
    ::closesocket(s);
#else
    ::close(s);
#endif
  }
  return BAD_SOCKET;
}

inline bool WouldBlock() {
#ifdef _WIN32
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EWOULDBLOCK;// || errno == EAGAIN;
#endif
}

bool Socket::ListenByHost(const std::string& host, int port, int backlog) {
  struct addrinfo hints, *result;
  memset(&hints, 0, sizeof(struct addrinfo));

  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_string[16];
  sprintf(port_string, "%d", port);

  coro_->BeginCompute();
  int rc = ::getaddrinfo(host.c_str(), port_string, &hints, &result);
  coro_->EndCompute();
  if (rc != 0) {
    return false;
  }

  struct addrinfo* addr;
  for (struct addrinfo* a = result; a && s_ == BAD_SOCKET; a = a->ai_next) {
    if (a->ai_family == AF_INET6) {
      s_ = CreateListenSocket(a->ai_family, a->ai_socktype, a->ai_protocol);
      addr = a;
    }
  }

  for (struct addrinfo* a = result; a && s_ == BAD_SOCKET; a = a->ai_next) {
    if (a->ai_family == AF_INET) {
      s_ = CreateListenSocket(a->ai_family, a->ai_socktype, a->ai_protocol);
      addr = a;
    }
  }

  if (s_ == BAD_SOCKET) {
    freeaddrinfo(result);
    return false;
  }

  if (bind(s_, addr->ai_addr, addr->ai_addrlen) || listen(s_, backlog)) {
    s_ = CloseSocket(s_);
    freeaddrinfo(result);
    return false;
  }

  freeaddrinfo(result);
  uv_poll_init_socket(coro_->GetScheduler()->GetLoop(), &poll_, s_);
  return true;
}

bool Socket::ListenByIp(const std::string& ip, int port, int backlog) {
  s_ = CreateListenSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s_ == BAD_SOCKET) {
    return false;
  }

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip.c_str());
  addr.sin_port = htons(port);

  if (bind(s_, (struct sockaddr*)&addr, sizeof addr) || listen(s_, backlog)) {
    s_ = CloseSocket(s_);
    return false;
  }

  uv_poll_init_socket(coro_->GetScheduler()->GetLoop(), &poll_, s_);
  return true;
}

void Socket::Close() {
  if (s_ != BAD_SOCKET) {
    uv_close(reinterpret_cast<uv_handle_t*>(&poll_), [](uv_handle_t* h) {
      ((Socket*)h->data)->coro_->SetEvent(EVENT_CONT);
    });
    coro_->Suspend(STATE_WAITING);
    s_ = CloseSocket(s_);
  }
}

bool Socket::ConnectHost(const std::string& host, int port) {
  struct addrinfo hints, *result;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_string[16];
  sprintf(port_string, "%d", port);

  coro_->BeginCompute();
  int rc = getaddrinfo(host.c_str(), port_string, &hints, &result);
  coro_->EndCompute();
  if (rc != 0) {
    return false;
  }

  s_ = CreateSocket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (s_ == BAD_SOCKET) {
    freeaddrinfo(result);
    return false;
  }

  rc = ::connect(s_, result->ai_addr, result->ai_addrlen);
  freeaddrinfo(result);
  if (rc == 0) {
    return true;
  }

  if (!WouldBlock()) {
    s_ = CloseSocket(s_);
    return false;
  }

  Event ev = WaitWritable();
  if (ev != EVENT_WRITABLE) {
    s_ = CloseSocket(s_);
    return false;
  }

  uv_poll_init_socket(coro_->GetScheduler()->GetLoop(), &poll_, s_);
  return true;
}

bool Socket::ConnectIp(const std::string& ip, int port) {
  s_ = CreateSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s_ == BAD_SOCKET) {
    return false;
  }

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip.c_str());
  addr.sin_port = htons(port);
  int rc = ::connect(s_, (struct sockaddr*)&addr, sizeof(addr));

  if (rc == 0) {
    return true;
  }

  if (!WouldBlock()) {
    s_ = CloseSocket(s_);
    return false;
  }

  Event ev = WaitWritable();
  if (ev != EVENT_WRITABLE) {
    s_ = CloseSocket(s_);
    return false;
  }

  uv_poll_init_socket(coro_->GetScheduler()->GetLoop(), &poll_, s_);
  return true;
}

int Socket::ReadSome(char* data, int len) {
  for (;;) {
    int rc = ::recv(s_, data, len, 0);
    if (rc >= 0) {
      return rc;
    }
    if (!WouldBlock()) {
      return rc;
    }
    Event ev = WaitReadable();
    if (ev != EVENT_READABLE) {
      return -1;
    }
  }
}

int Socket::ReadAtLeast(char* data, int len, int min_len) {
  assert(min_len < len);
  int size = 0;
  while (size < min_len) {
    int rc = ReadSome(data + size, len - size);
    if (rc == 0) { // EOF
      return size;
    }
    if (rc < 0) {
      return size;
    }
    size += rc;
  }
  return size;
}

int Socket::WriteSome(const char* data, int len) {
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
  for (;;) {
    int rc = ::send(s_, data, len, MSG_NOSIGNAL);
    if (rc > 0) {
      return rc;
    }
    if (!WouldBlock()) {
      return rc;
    }
    Event ev = WaitWritable();
    if (ev != EVENT_WRITABLE) {
      return -1;
    }
  }
}

int Socket::WriteExactly(const char* data, int len) {
  int size = 0;
  while (size < len) {
    int rc = WriteSome(data + size, len - size);
    if (rc <= 0) {
      return size;
    }
    size += rc;
  }
  return size;
}

uv_os_sock_t Socket::Accept() {
  for (;;) {
    struct sockaddr_storage mem;
    socklen_t len = sizeof(mem);
    uv_os_sock_t new_s = BAD_SOCKET;
#if defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
    new_s = accept4(s_, (struct sockaddr*)&mem, &len, SOCK_CLOEXEC | SOCK_NONBLOCK);
#else
    new_s = ::accept(s_, (struct sockaddr*)&mem, &len);
#endif
    if (new_s != BAD_SOCKET) {
      return SetNonblocking(SetNoSigPipe(new_s));
    }
    if (!WouldBlock()) {
      return -1;
    }
    Event ev = WaitReadable();
    if (ev != EVENT_READABLE) {
      return -1;
    }
  }
}

Event Socket::WaitWritable() {
  int events = 0;
  events |= UV_WRITABLE;
  events |= UV_DISCONNECT;
  uv_poll_start(&poll_, events, [](uv_poll_t* w, int status, int events) {
    if (status > 0) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_HUP);
    } else if (events & UV_WRITABLE) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_WRITABLE);
    } else if (events & UV_DISCONNECT) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_HUP);
    }
  });
  coro_->SetTimeout(GetDeadline());
  coro_->Suspend(STATE_WAITING);
  uv_poll_stop(&poll_);
  return coro_->GetEvent();
}

Event Socket::WaitReadable(Condition* cond) {
  int events = 0;
  events |= UV_READABLE;
  events |= UV_DISCONNECT;
  uv_poll_start(&poll_, events, [](uv_poll_t* w, int status, int events) {
    if (status > 0) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_HUP);
    } else if (events & EVENT_READABLE) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_READABLE);
    } else if (events & UV_DISCONNECT) {
      ((Socket*)w->data)->coro_->SetEvent(EVENT_HUP);
    }
  });
  coro_->SetTimeout(GetDeadline());
  if (cond) {
    cond->Wait(coro_);
  } else {
    coro_->Suspend(STATE_WAITING);
  }
  uv_poll_stop(&poll_);
  return coro_->GetEvent();
}

}
