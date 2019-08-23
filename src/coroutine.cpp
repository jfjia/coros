#include "coros.hpp"
#include <cassert>
#include <atomic>

namespace coros {

bool Coroutine::create(Scheduler* sched, const Callback& fn, const Callback& exit_fn) {
    sched_ = sched;
    id_ = sched->next_coro_id();
    fn_ = fn;
    exit_fn_ = exit_fn;
    stack_ = sched->allocate_stack();
    if (!stack_.sp) {
        return false;
    }
    ctx_ = context::make_fcontext(stack_.sp, stack_.size, [](context::transfer_t t) {
        ((Coroutine*)t.data)->caller_ = t.fctx;
        try {
            ((Coroutine*)t.data)->fn_();
        } catch (Unwind& uw) {
        }
        ((Coroutine*)t.data)->state_ = STATE_DONE;
        context::jump_fcontext(((Coroutine*)t.data)->caller_, NULL);
    });
    return true;
}

void Coroutine::destroy() {
    if (joined_) {
        joined_->set_event(EVENT_JOIN);
    }
    sched_->deallocate_stack(stack_);
    exit_fn_();
}

}
