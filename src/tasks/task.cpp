#include <tasks/task.h>

#include <tasks/task_scheduler.h>

#include <cassert>

// wz::tasks -- run()/wait() dispatch.
//
// No scheduler installed: run() executes each task inline and wait() is immediate
// -- the serial S0 backend (and the S3 force-serial path).
//
// Scheduler installed: run() SUBMITS every task to the pool -- including from a
// worker (S2), so a node's nested children are stealable and the recursion goes
// parallel, not just the flat top level. wait() on the main/engine thread blocks
// on the counter via atomic wait/notify (the "safe island"); wait() on a WORKER
// parks the running task fibre and lets that worker steal other work, resuming
// when the counter drains -- so no worker ever blocks and the pool stays
// deadlock-free.

namespace wz::tasks
{
    void run(Counter& c, TaskFn fn, void* user)
    {
        TaskScheduler* s = get_task_scheduler();
        if (s == nullptr || force_serial())
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
        if (s == nullptr || force_serial())
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
        if (s == nullptr || force_serial())
        {
            assert(c.outstanding_.load(std::memory_order_acquire) == 0 &&
                "wz::tasks::wait: work still outstanding on the serial path");
            return;
        }

        if (is_worker_thread())
        {
            // A task fibre on a worker: park + steal other work until c drains.
            s->wait_on_worker(c);
            return;
        }

        // Main/engine thread (the safe island): block until the pool drains c.
        s->wait_on_main(c);
    }
}
