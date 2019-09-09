#include "coros.hpp"
#include "malog.h"
#include <string.h>
#include <memory.h>

class Conn {
public:
    bool start(uv_os_sock_t fd) {
        if (!coro_.create(coros::Scheduler::get(), std::bind(&Conn::fn, this, fd), std::bind(&Conn::exit_fn, this))) {
            return false;
        }
        return true;
    }

    void fn(uv_os_sock_t fd) {
        coros::Socket s(fd);
        char buf[256];
        for (;;) {
            int len = s.read_some(buf, 256);
            malog_info("in bytes: %d", len);
            if (len <= 0) {
                break;
            }
            malog_info("in string: %.*s", len, buf);
            len = s.write_some(buf, len);
            malog_info("out bytes: %d", len);
            if (strncmp(buf, "exit", 4) == 0) {
                break;
            }
        }
        s.close();
    }

    void exit_fn() {
        MALOG_INFO("coro[" << coro_.id() << "] exit");
        delete this;
    }

private:
    coros::Coroutine coro_;
};

class Listener {
public:
    bool start() {
        if (!coro_.create(coros::Scheduler::get(), std::bind(&Listener::fn, this), std::bind(&Listener::exit_fn, this))) {
            return false;
        }
        return true;
    }

    void fn() {
        coros::Socket s;
        s.listen_by_ip("0.0.0.0", 9090);
        for (;;) {
            uv_os_sock_t s_new = s.accept();
            malog_info("new conn");
            if (s_new == BAD_SOCKET) {
                malog_error("bad accept");
                break;
            }
            Conn* c = new Conn();
            c->start(s_new);
        }
    }

    void exit_fn() {
    }

protected:
    coros::Coroutine coro_;
};

int main(int argc, char** argv) {
    MALOG_OPEN_STDIO(1, 0, true);
    coros::Scheduler sched(true);
    Listener l;
    l.start();
    sched.run();
    return 0;
}
