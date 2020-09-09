#include "coros.h"
#include <cassert>
#include <atomic>

namespace coros {

#define alignment16(a) (((a)+0x0F)&(~0x0F))
static const std::size_t kReservedSize = alignment16(sizeof(Coroutine) + 64);

Coroutine* Coroutine::Create(Scheduler* sched,
                             const std::function<void()>& fn,
                             const std::function<void(Coroutine*)>& exit_fn,
                             std::size_t stack_size) {
  if (!sched) {
    sched = Scheduler::Get();
  }

  context::Stack stack = context::AllocateStack(stack_size ? stack_size : sched->GetStackSize());
  if (!stack.sp) {
    return nullptr;
  }

  Coroutine* c = new (static_cast<char*>(stack.sp) - kReservedSize + 32)Coroutine;
  stack.sp = static_cast<char*>(stack.sp) - kReservedSize;
  stack.size -= kReservedSize;

  c->stack_ = stack;
  c->sched_ = sched;
  c->id_ = NextId();
  c->fn_ = fn;
  c->exit_fn_ = exit_fn;
  c->ctx_ = context::make_fcontext(stack.sp, stack.size, [](context::transfer_t t) {
    ((Coroutine*)t.data)->caller_ = t.fctx;
    try {
      ((Coroutine*)t.data)->fn_();
    } catch (Unwind& uw) {
    }
    ((Coroutine*)t.data)->state_ = STATE_DONE;
    context::jump_fcontext(((Coroutine*)t.data)->caller_, NULL);
  });
  if (sched != Scheduler::Get()) {
    sched->PostCoroutine(c, false);
  } else {
    sched->AddCoroutine(c);
  }

  return c;
}

void Coroutine::Destroy() {
  if (joined_) {
    joined_->Wakeup(EVENT_JOIN);
  }
  exit_fn_(this);
  stack_.sp = static_cast<char*>(stack_.sp) + kReservedSize;
  stack_.size += kReservedSize;
  this->~Coroutine();
  context::DeallocateStack(stack_);
}

std::size_t Coroutine::NextId() {
  static std::atomic<std::size_t> next_id{ 1 };
  return next_id.fetch_add(1);
}

} // coros
