#include "coros.hpp"
#include <cassert>
#include <atomic>

namespace coros {

bool Coroutine::Create(Scheduler* sched,
                       const std::function<void()>& fn,
                       const std::function<void()>& exit_fn) {
  if (!sched) {
    sched = Scheduler::Get();
  }
  sched_ = sched;
  id_ = NextId() * 1000 + sched_->GetId();
  fn_ = fn;
  exit_fn_ = exit_fn;
  stack_ = context::AllocateStack(sched->GetStackSize());
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
  if (sched_ != Scheduler::Get()) {
    sched_->PostCoroutine(this, false);
  } else {
    sched->AddCoroutine(this);
  }
  return true;
}

void Coroutine::Destroy() {
  if (joined_) {
    joined_->Wakeup(EVENT_JOIN);
  }
  context::DeallocateStack(stack_);
  exit_fn_();
}

std::size_t Coroutine::NextId() {
  static std::atomic<std::size_t> next_id{ 1 };
  return next_id.fetch_add(1);
}

}
