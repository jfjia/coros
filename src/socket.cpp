#include "coros.hpp"
#include "malog.h"
#include <cassert>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>

namespace coros {
inline uv_os_sock_t set_no_sigpipe(uv_os_sock_t s) {
#ifdef SO_NOSIGPIPE
    if (s != BAD_SOCKET) {
        int no_sigpipe = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(int));
    }
#endif
    return s;
}

inline uv_os_sock_t set_nonblocking(uv_os_sock_t s) {
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

inline uv_os_sock_t create_socket(int domain, int type, int protocol) {
    int flags = 0;
#if defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
    flags = SOCK_CLOEXEC | SOCK_NONBLOCK;
#endif
    return set_nonblocking(set_no_sigpipe(socket(domain, type | flags, protocol)));
}

inline uv_os_sock_t create_listen_socket(int domain, int type, int protocol) {
    uv_os_sock_t s = create_socket(domain, type, protocol);
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

inline uv_os_sock_t close_socket(uv_os_sock_t s) {
    if (s != BAD_SOCKET) {
#if defined(_WIN32)
        ::closesocket(s);
#else
        ::close(s);
#endif
    }
    return BAD_SOCKET;
}

inline bool would_block() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK;// || errno == EAGAIN;
#endif
}

bool Socket::listen_by_host(const std::string& host, int port, int backlog) {
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_string[16];
    sprintf(port_string, "%d", port);

    coro_->begin_compute();
    int rc = ::getaddrinfo(host.c_str(), port_string, &hints, &result);
    coro_->end_compute();
    if (rc != 0) {
        return false;
    }

    struct addrinfo* addr;
    for (struct addrinfo* a = result; a && s_ == BAD_SOCKET; a = a->ai_next) {
        if (a->ai_family == AF_INET6) {
            s_ = create_listen_socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            addr = a;
        }
    }

    for (struct addrinfo* a = result; a && s_ == BAD_SOCKET; a = a->ai_next) {
        if (a->ai_family == AF_INET) {
            s_ = create_listen_socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            addr = a;
        }
    }

    if (s_ == BAD_SOCKET) {
        freeaddrinfo(result);
        return false;
    }

    if (bind(s_, addr->ai_addr, addr->ai_addrlen) || listen(s_, backlog)) {
        s_ = close_socket(s_);
        freeaddrinfo(result);
        return false;
    }

    freeaddrinfo(result);
    uv_poll_init_socket(coro_->sched()->loop(), &poll_, s_);
    return true;
}

bool Socket::listen_by_ip(const std::string& ip, int port, int backlog) {
    s_ = create_listen_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_ == BAD_SOCKET) {
        return false;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons(port);

    if (bind(s_, (struct sockaddr*)&addr, sizeof addr) || listen(s_, backlog)) {
        s_ = close_socket(s_);
        return false;
    }

    uv_poll_init_socket(coro_->sched()->loop(), &poll_, s_);
    return true;
}

void Socket::close() {
    if (s_ != BAD_SOCKET) {
        uv_close(reinterpret_cast<uv_handle_t*>(&poll_), [](uv_handle_t* h) {
            ((Socket*)h->data)->coro_->set_event(EVENT_CONT);
        });
        coro_->yield(STATE_WAITING);
        s_ = close_socket(s_);
    }
}

bool Socket::connect_host(const std::string& host, int port) {
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_string[16];
    sprintf(port_string, "%d", port);

    coro_->begin_compute();
    int rc = getaddrinfo(host.c_str(), port_string, &hints, &result);
    coro_->end_compute();
    if (rc != 0) {
        return false;
    }

    s_ = create_socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (s_ == BAD_SOCKET) {
        freeaddrinfo(result);
        return false;
    }

    rc = ::connect(s_, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    if (rc == 0) {
        return true;
    }

    if (!would_block()) {
        s_ = close_socket(s_);
        return false;
    }

    Event ev = wait(WAIT_WRITABLE);
    if (ev != EVENT_WRITABLE && ev != EVENT_RWABLE) {
        s_ = close_socket(s_);
        return false;
    }

    uv_poll_init_socket(coro_->sched()->loop(), &poll_, s_);
    return true;
}

bool Socket::connect_ip(const std::string& ip, int port) {
    s_ = create_listen_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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

    if (!would_block()) {
        s_ = close_socket(s_);
        return false;
    }

    Event ev = wait(WAIT_WRITABLE);
    if (ev != EVENT_WRITABLE && ev != EVENT_RWABLE) {
        s_ = close_socket(s_);
        return false;
    }

    uv_poll_init_socket(coro_->sched()->loop(), &poll_, s_);
    return true;
}

int Socket::read(char* data, int len) {
    for (;;) {
        int rc = ::recv(s_, data, len, 0);
        malog_debug("::recv(%d)=%d", s_, rc);
        if (rc >= 0) {
            return rc;
        }
        if (!would_block()) {
            malog_error("::recv() failed and cannot retry");
            return rc;
        }
        Event ev = wait(WAIT_READABLE);
        malog_debug("wait(WAIT_READABLE)=%d", ev);
        if (ev != EVENT_READABLE && ev != EVENT_RWABLE) {
            return -1;
        }
    }
}

int Socket::write(const char* data, int len) {
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
    for (;;) {
        int rc = ::send(s_, data, len, MSG_NOSIGNAL);
        malog_debug("::send(%d)=%d", s_, rc);
        if (rc > 0) {
            return rc;
        }
        if (!would_block()) {
            malog_error("::send() failed and cannot retry");
            return rc;
        }
        Event ev = wait(WAIT_WRITABLE);
        malog_debug("wait(WAIT_WRITABLE)=%d", ev);
        if (ev != EVENT_WRITABLE && ev != EVENT_RWABLE) {
            return -1;
        }
    }
}

uv_os_sock_t Socket::accept() {
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
            return set_nonblocking(set_no_sigpipe(new_s));
        }
        if (!would_block()) {
            return -1;
        }
        Event ev = wait(WAIT_READABLE);
        if (ev != EVENT_READABLE) {
            return -1;
        }
    }
}

}
