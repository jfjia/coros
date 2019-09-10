#include "coros.hpp"
#include "malog.h"

class MyCo {
public:
    bool start(coros::Scheduler* sched) {
        return coro_.create(sched, std::bind(&MyCo::fn, this), std::bind(&MyCo::exit_fn, this));
    }

    void fn() {
        malog_info("coro[%lld]: fn()", coro_.id());
        coro_.wait(500);
        malog_info("coro[%lld]: wait()", coro_.id());
        coro_.begin_compute();
        malog_info("coro[%lld]: run in compute threads", coro_.id());
        coro_.end_compute();
        malog_info("coro[%lld]: back in coro thread", coro_.id());
    }

    void exit_fn() {
        malog_info("coro[%lld]: exit_fn()", coro_.id());
        delete this;
    }

    coros::Coroutine* coro() {
        return &coro_;
    }

private:
    coros::Coroutine coro_;
};

int main(int argc, char** argv) {
    MALOG_OPEN_STDIO(1, 0, true);
    coros::Scheduler sched(true);
    MyCo* co = new MyCo();
    co->start(&sched);
    sched.run();
    return 0;
}
