#include "coros.hpp"
#include <cassert>
#include <cmath>
#include <atomic>
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

static const int SWEEP_INTERVAL = 1000;

static const int RECV_BUFFER_SIZE = 256 * 1024;

bool Scheduler::is_stack_unbounded_;
std::size_t Scheduler::page_size_;
std::size_t Scheduler::min_stack_size_;
std::size_t Scheduler::max_stack_size_;
std::size_t Scheduler::default_stack_size_;

Scheduler::Scheduler(bool is_default)
    : is_default_(is_default) {
    if (is_default) {
#if defined(_WIN32)
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);

        SYSTEM_INFO si;
        GetSystemInfo(&si);
        page_size_ = (std::size_t)si.dwPageSize;

        is_stack_unbounded_ = true;
        min_stack_size_ = 8 * 1024;
        max_stack_size_ = 1024 * 1024;
        default_stack_size_ = 32 * 1024;
#else
        is_stack_unbounded_ = true;
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
        loop_ptr_ = uv_default_loop();
    } else {
        loop_ptr_ = &loop_;
        uv_loop_init(loop_ptr_);
        loop_ptr_->data = this;
    }

    pre_.data = (void*)this;
    uv_prepare_init(loop_ptr_, &pre_);
    uv_prepare_start(&pre_, [](uv_prepare_t* w) {
        ((Scheduler*)w->data)->pre();
    });

    check_.data = (void*)this;
    uv_check_init(loop_ptr_, &check_);
    uv_check_start(&check_, [](uv_check_t* w) {
        ((Scheduler*)w->data)->check();
    });

    async_.data = (void*)this;
    uv_async_init(loop_ptr_, &async_, [](uv_async_t* w) {
        ((Scheduler*)w->data)->async();
    });

    sweep_timer_.data = (void*)this;
    uv_timer_init(loop_ptr_, &sweep_timer_);
    uv_timer_start(&sweep_timer_, [](uv_timer_t* w) {
        ((Scheduler*)w->data)->sweep();
    }, SWEEP_INTERVAL, SWEEP_INTERVAL);

    recv_buf_ = new char[RECV_BUFFER_SIZE];
}

void Scheduler::pre() {
    check();
}

template<typename T>
inline void fast_del_vector_item(std::vector<T>& v, std::size_t i) {
    assert(i >= 0 && i < v.size());
    v[i] = v[v.size() - 1];
    v.pop_back();
}

void Scheduler::check() {
    for (std::size_t i = 0; i < waiting_.size();) {
        Coroutine* c = waiting_[i];
        if (c->state() == STATE_READY) {
            ready_.push_back(c);
            fast_del_vector_item<Coroutine* >(waiting_, i);
            continue;
        }
        i++;
    }
    run_coros();
}

void Scheduler::async() {
    lock_.lock();
    ready_.insert(ready_.end(), posted_.begin(), posted_.end());
    posted_.clear();
    lock_.unlock();
}

void Scheduler::sweep() {
    for (std::size_t i = 0; i < waiting_.size();) {
        Coroutine* c = waiting_[i];
        c->check_timeout();
        if (c->state() == STATE_READY) {
            ready_.push_back(c);
            fast_del_vector_item<Coroutine* >(waiting_, i);
            continue;
        }
        i++;
    }
}

Scheduler::~Scheduler() {
    uv_loop_close(loop_ptr_);
    if (is_default_) {
#if defined(_WIN32)
        WSACleanup();
#endif
    }
    delete []recv_buf_;
}

std::size_t Scheduler::next_coro_id() {
    static std::atomic<std::size_t> next_id{ 1 };
    return next_id.fetch_add(1);
}

void Scheduler::add_coroutine(Coroutine* coro) {
    switch (coro->state()) {
    case STATE_READY:
        ready_.push_back(coro);
        break;
    case STATE_WAITING:
        waiting_.push_back(coro);
        break;
    case STATE_DONE:
        coro->destroy();
        break;
    default:
        assert(false);
        break;
    }
}

void Scheduler::run() {
    uv_run(loop_ptr_, UV_RUN_DEFAULT);
    cleanup(ready_);
    cleanup(waiting_);
    uv_timer_stop(&sweep_timer_);
    uv_check_stop(&check_);
    uv_prepare_stop(&pre_);
    uv_close(reinterpret_cast<uv_handle_t*>(&sweep_timer_), [](uv_handle_t* h) {});
    uv_close(reinterpret_cast<uv_handle_t*>(&async_), [](uv_handle_t* h) {});
    uv_close(reinterpret_cast<uv_handle_t*>(&check_), [](uv_handle_t* h) {});
    uv_close(reinterpret_cast<uv_handle_t*>(&pre_), [](uv_handle_t* h) {});
    uv_run(loop_ptr_, UV_RUN_NOWAIT);
}

void Scheduler::run_coros() {
    while (ready_.size() > 0) {
        for (std::size_t i = 0; i < ready_.size();) {
            Coroutine* c = ready_[i];
            current_ = c;
            c->resume();
            current_ = nullptr;
            if (c->state() == STATE_DONE) {
                c->destroy();
                fast_del_vector_item<Coroutine*>(ready_, i);
                continue;
            } else if (c->state() == STATE_WAITING) {
                waiting_.push_back(c);
                fast_del_vector_item<Coroutine*>(ready_, i);
                continue;
            } else if (c->state() == STATE_COMPUTE) {
                fast_del_vector_item<Coroutine*>(ready_, i);
                outstanding_ ++;
            }
            i++;
        }
    }
    if (waiting_.size() == 0 && outstanding_ == 0) {
        uv_stop(loop_ptr_);
    }
}

void Scheduler::wait(Coroutine* coro, long millisecs) {
    uv_timer_t timer;
    timer.data = (void*)coro;
    uv_timer_init(loop_ptr_, &timer);
    uv_timer_start(&timer, [](uv_timer_t* w) {
        uv_timer_stop(w);
        uv_close(reinterpret_cast<uv_handle_t*>(w), [](uv_handle_t* w) {
            ((Coroutine*)w->data)->set_event(EVENT_TIMEOUT);
        });
    }, millisecs, 0);
    coro->yield(STATE_WAITING);
}

void Scheduler::wait(Coroutine* coro, Socket& s, int flags) {
    int events = 0;
    if (flags & WAIT_READABLE) {
        events |= UV_READABLE;
    }
    if (flags & WAIT_WRITABLE) {
        events |= UV_WRITABLE;
    }
    uv_poll_start(&s.poll_, events, [](uv_poll_t* w, int status, int events) {
        if (status > 0) {
            ((Socket*)w->data)->coro_->set_event(EVENT_HUP);
        } else if ((events & UV_READABLE) && (events & UV_WRITABLE)) {
            ((Socket*)w->data)->coro_->set_event(EVENT_RWABLE);
        } else if (events & UV_READABLE) {
            ((Socket*)w->data)->coro_->set_event(EVENT_READABLE);
        } else if (events & UV_WRITABLE) {
            ((Socket*)w->data)->coro_->set_event(EVENT_WRITABLE);
        } else {
            //???TODO
        }
    });
    s.coro_->yield(STATE_WAITING);
    uv_poll_stop(&s.poll_);
}

void Scheduler::cleanup(CoroutineList& cl) {
    for (auto c : cl) {
        c->set_event(EVENT_CANCEL);
        c->resume();
        c->destroy();
    }
    cl.clear();
}

void Scheduler::post_coroutine(Coroutine* coro) {
    {
        std::lock_guard<std::mutex> l(lock_);
        posted_.push_back(coro);
    }
    uv_async_send(&async_);
}

void Scheduler::begin_compute(Coroutine* coro) {
    uv_work_t c;
    c.data = (void*)coro;
    uv_queue_work(loop_ptr_, &c, [](uv_work_t* w) {
        ((Coroutine*)w->data)->set_event(EVENT_COMPUTE);
        ((Coroutine*)w->data)->resume();
    }, [](uv_work_t* w, int status) {
        ((Coroutine*)w->data)->set_event(EVENT_COMPUTE_DONE);
        ((Coroutine*)w->data)->sched()->add_coroutine((Coroutine*)w->data);
        ((Coroutine*)w->data)->sched()->outstanding_ --;
    });
    coro->yield(STATE_COMPUTE);
}

Stack Scheduler::allocate_stack() {
    const std::size_t pages(static_cast<std::size_t>(std::ceil(static_cast<float>(default_stack_size_) / page_size_)));
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

void Scheduler::deallocate_stack(Stack& stack) {
    assert(stack.sp);
    void* vp = static_cast< char* >(stack.sp) - stack.size;
#if defined(_WIN32)
    ::VirtualFree(vp, 0, MEM_RELEASE);
#else
    munmap(vp, stack.size);
#endif
}

thread_local Scheduler* local_sched = nullptr;

Scheduler* Scheduler::get() {
    return local_sched;
}

Scheduler* Scheduler::create(bool is_default) {
    if (local_sched) {
        return local_sched;
    }
    Scheduler* sched = new Scheduler(is_default);
    local_sched = sched;
    return sched;
}

}
