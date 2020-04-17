#include "coros.hpp"
#include <cassert>
#include <atomic>

namespace coros {

bool Coroutine::Create(Scheduler* sched,
                       const std::function<void()>& fn,
                       const std::function<void()>& exit_fn) {
  sched_ = sched;
  id_ = sched->NextId();
  fn_ = fn;
  exit_fn_ = exit_fn;
  stack_ = sched->AllocateStack();
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
  sched->AddCoroutine(this);
  return true;
}

void Coroutine::Destroy() {
  if (joined_) {
    joined_->Wakeup(EVENT_JOIN);
  }
  sched_->DeallocateStack(stack_);
  exit_fn_();
}

}
