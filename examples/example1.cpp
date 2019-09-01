#include "coros.hpp"
#include "malog.h"

class MyCo {
public:
    MyCo(coros::Scheduler* sched) {
        coro_.create(sched, std::bind(&MyCo::fn, this), std::bind(&MyCo::exit_fn, this));
    }

    void fn() {
        malog_info("fn()");
        coro_.wait(500);
        malog_info("wait()");
    }

    void exit_fn() {
        malog_info("exit_fn()");
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
    coros::Scheduler* sched = coros::Scheduler::create();
    MyCo* co = new MyCo(sched);
    sched->add_coroutine(co->coro());
    sched->run();
    delete sched;
    return 0;
}
