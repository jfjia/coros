#include "coros.hpp"
#include "malog.h"
#include <string.h>
#include <memory.h>

class Conn {
public:
    Conn(coros::Scheduler* sched, uv_os_sock_t s) : s_(s) {
        coro_.create(sched, std::bind(&Conn::fn, this), std::bind(&Conn::exit_fn, this));
        s_.attach(&coro_);
    }

    void fn() {
        char buf[256];
        for (;;) {
            int len = s_.read_some(buf, 256);
            malog_info("in bytes: %d", len);
            if (len <= 0) {
                break;
            }
            malog_info("in string: %.*s", len, buf);
            len = s_.write_some(buf, len);
            malog_info("out bytes: %d", len);
            if (strncmp(buf, "exit", 4) == 0) {
                break;
            }
        }
        s_.close();
    }

    void exit_fn() {
        delete this;
    }

    coros::Coroutine* coro() {
        return &coro_;
    }

private:
    coros::Coroutine coro_;
    coros::Socket s_;
};

class Listener {
public:
    Listener(coros::Scheduler* sched) {
        coro_.create(sched, std::bind(&Listener::fn, this), std::bind(&Listener::exit_fn, this));
        s_.attach(&coro_);
    }

    void fn() {
        s_.listen_by_ip("0.0.0.0", 9090);
        for (;;) {
            uv_os_sock_t s_new = s_.accept();
            malog_info("new conn");
            if (s_new == BAD_SOCKET) {
                malog_error("bad accept");
                break;
            }
            Conn* c = new Conn(coro_.sched(), s_new);
            coro_.sched()->add_coroutine(c->coro());
        }
    }

    void exit_fn() {
        delete this;
    }

    coros::Coroutine* coro() {
        return &coro_;
    }
protected:
    coros::Coroutine coro_;
    coros::Socket s_;
};

int main(int argc, char** argv) {
    MALOG_OPEN_STDIO(1, 0, true);
    coros::Scheduler* sched = coros::Scheduler::create();
    Listener* l = new Listener(sched);
    sched->add_coroutine(l->coro());
    sched->run();
    delete sched;
    return 0;
}
