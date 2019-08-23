#include "coros.hpp"
#include "log.hpp"

class MyCo {
public:
    MyCo(coros::Scheduler* sched) {
        coro_.create(sched, std::bind(&MyCo::fn, this), std::bind(&MyCo::exit_fn, this));
    }

    void fn() {
        log_info("fn()");
        coro_.wait(500);
        log_info("wait()");
    }

    void exit_fn() {
        log_info("exit_fn()");
        delete this;
    }

    coros::Coroutine* coro() {
        return &coro_;
    }

private:
    coros::Coroutine coro_;
};

int main(int argc, char** argv) {
    LOG_OPEN("@stdout", 1, 0, coros::log::POLICY_WAIT);
    coros::Scheduler* sched = coros::Scheduler::create();
    MyCo* co = new MyCo(sched);
    sched->add_coroutine(co->coro());
    sched->run();
    delete sched;
    return 0;
}
