#include <tasks/task.h>

#include <tasks/task_scheduler.h>

#include <cassert>

// wz::tasks -- run()/wait() dispatch.
//
// With no scheduler installed, or when called NESTED from a worker thread, run()
// executes each task inline on the calling thread and wait() is immediate: the
// serial S0 backend (and the S3 force-serial path). Nested-on-a-worker staying
// serial is what keeps the recursion on the worker's own stack and the pool
// deadlock-free at S1 -- a worker never blocks in wait().
//
// With a scheduler installed and called from a non-worker (the engine/main
// thread), run() fans tasks onto the pool and wait() blocks on the counter via
// atomic wait/notify while the workers drain it.

namespace wz::tasks
{
    namespace
    {
        bool serial_here(TaskScheduler* s)
        {
            return s == nullptr || is_worker_thread();
        }
    }

    void run(Counter& c, TaskFn fn, void* user)
    {
        TaskScheduler* s = get_task_scheduler();
        if (serial_here(s))
        {
            c.outstanding_.fetch_add(1, std::memory_order_relaxed);
            fn(user);
            c.outstanding_.fetch_sub(1, std::memory_order_relaxed);
            return;
        }
        c.outstanding_.fetch_add(1, std::memory_order_relaxed);
        s->submit(Task{ &c, user, fn, nullptr, 0 });
    }

    void run(Counter& c, std::size_t n, IndexFn fn, void* user)
    {
        if (n == 0)
            return;

        TaskScheduler* s = get_task_scheduler();
        if (serial_here(s))
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                c.outstanding_.fetch_add(1, std::memory_order_relaxed);
                fn(i, user);
                c.outstanding_.fetch_sub(1, std::memory_order_relaxed);
            }
            return;
        }
        c.outstanding_.fetch_add(static_cast<int>(n), std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i)
            s->submit(Task{ &c, user, nullptr, fn, i });
    }

    void wait(Counter& c)
    {
        TaskScheduler* s = get_task_scheduler();
        if (serial_here(s))
        {
            // Serial / nested-on-worker: every task ran during run(), so nothing
            // is outstanding.
            assert(c.outstanding_.load(std::memory_order_acquire) == 0 &&
                "wz::tasks::wait: work still outstanding on the serial path");
            return;
        }

        // Non-worker thread with a pool installed: block until the workers drain
        // this counter. atomic::wait re-checks the value, so a decrement that
        // races the load between the check and the wait cannot be missed.
        for (;;)
        {
            const int v = c.outstanding_.load(std::memory_order_acquire);
            if (v == 0)
                return;
            c.outstanding_.wait(v, std::memory_order_acquire);
        }
    }
}
