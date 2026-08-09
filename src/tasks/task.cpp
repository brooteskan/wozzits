#include <tasks/task.h>

#include <cassert>

// wz::tasks -- S0 SERIAL-INLINE backend.
//
// run() executes each task immediately on the calling thread, bracketing it with
// a transient increment/decrement of the counter, so by the time run() returns
// the counter is back to zero and wait() has nothing to do. Nested run()/wait()
// (a task that itself forks and joins) is therefore ordinary depth-first
// recursion here -- which is exactly why it reproduces the pre-#293 serial
// contraction bit-for-bit and serves as the correctness oracle.
//
// The worker pool (S1) and Win32 fibres (S2) replace THESE THREE FUNCTIONS behind
// the same signatures in task.h; this file also remains the S3 force-serial
// backend, so it is not throwaway scaffolding.

namespace wz::tasks
{
    void run(Counter& c, TaskFn fn, void* user)
    {
        ++c.outstanding_;
        fn(user);
        --c.outstanding_;
    }

    void run(Counter& c, std::size_t n, IndexFn fn, void* user)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            ++c.outstanding_;
            fn(i, user);
            --c.outstanding_;
        }
    }

    void wait(Counter& c)
    {
        // Serial inline: every task ran during run(), so nothing is outstanding.
        // The assert pins that invariant; once the pool lands this is where the
        // outer caller blocks and a worker parks its fibre (S2).
        assert(c.outstanding_ == 0 &&
            "wz::tasks::wait: work still outstanding under the serial-inline "
            "backend, which runs every task eagerly in run()");
        (void)c;
    }
}
