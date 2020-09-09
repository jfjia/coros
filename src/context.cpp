#include "coros.h"
#include <cmath>
#include <cassert>
#if !defined(_WIN32)
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

namespace coros {

namespace context {

static std::size_t page_size_;
static std::size_t min_stack_size_;
static std::size_t max_stack_size_;
static std::size_t default_stack_size_;

void InitStack() {
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  page_size_ = (std::size_t)si.dwPageSize;

  min_stack_size_ = 8 * 1024;
  max_stack_size_ = 1024 * 1024;
  default_stack_size_ = 32 * 1024;
#else
#if defined(SIGSTKSZ)
  default_stack_size_ = SIGSTKSZ;
#else
  default_stack_size_ = 32 * 1024;
#endif
#if defined(MINSIGSTKSZ)
  min_stack_size_ = MINSIGSTKSZ;
#else
  min_stack_size_ = 8 * 1024;
#endif
  page_size_ = (size_t)sysconf(_SC_PAGESIZE);
  struct rlimit limit;
  getrlimit(RLIMIT_STACK, &limit);
  max_stack_size_ = (size_t)limit.rlim_max;
#endif
}

Stack AllocateStack(std::size_t statck_size) {
  if (statck_size == 0) {
    statck_size = default_stack_size_;
  }

  const std::size_t pages(static_cast<std::size_t>(std::ceil(static_cast<float>(statck_size) / page_size_)));
  const std::size_t size__ = (pages + 1) * page_size_;
  Stack stack;
  void* vp = nullptr;

#if defined(_WIN32)
  vp = ::VirtualAlloc(0, size__, MEM_COMMIT, PAGE_READWRITE);
  if (! vp) {
    return stack;
  }
  DWORD old_options;
  ::VirtualProtect(vp, page_size_, PAGE_READWRITE | PAGE_GUARD /*PAGE_NOACCESS*/, &old_options);
#else
# if defined(MAP_ANON)
  vp = mmap(0, size__, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
# else
  vp = mmap(0, size__, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
# endif
  if (vp == MAP_FAILED) {
    return stack;
  }
  mprotect(vp, page_size_, PROT_NONE);
#endif

  stack.size = size__;
  stack.sp = static_cast< char* >(vp) + stack.size;
  return stack;
}

void DeallocateStack(Stack& stack) {
  assert(stack.sp);
  void* vp = static_cast< char* >(stack.sp) - stack.size;
#if defined(_WIN32)
  ::VirtualFree(vp, 0, MEM_RELEASE);
#else
  munmap(vp, stack.size);
#endif
}

} // context

} // coros
