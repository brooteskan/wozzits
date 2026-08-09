#pragma once

// tasks/task.h
//
// wz::tasks -- the fork-join task API (#293). run() submits work against a
// Counter; wait() returns once that counter reaches zero. This is the S0 surface:
// the exact signatures the cognition tensor-network contraction (and every later
// consumer) calls, sitting on a SERIAL-INLINE backend -- run() executes each task
// immediately on the calling thread, so a counter is already drained by the time
// its wait() runs.
//
// The point of S0 is to pin the API and establish the correctness oracle: the
// contraction rewritten onto run()/wait() produces bit-identical results because
// the backend is plain in-order recursion. The worker pool + work-stealing (S1)
// and Win32 fibres for the mid-task nested wait (S2) slot in behind these SAME
// three signatures later; this serial backend also stays as the S3
// force-serial / determinism-oracle path, so it is not throwaway scaffolding.
//
// The task payload is a raw function pointer + void* user (zero-alloc, mirroring
// jobs::JobFn) rather than std::function: a fork-join tree spawns one task per
// subtree and must not allocate per node. Captured state rides in `user`. A
// non-capturing lambda converts to the function-pointer form, so call sites stay
// readable without heap closures.

#include <cstddef>

namespace wz::tasks
{
    class Counter;

    // A task: called with the caller-supplied `user` pointer.
    using TaskFn = void (*)(void* user);

    // An index task: called as fn(i, user) for each i in a fan-out range. The
    // natural form for parallel-over-children / parallel-for.
    using IndexFn = void (*)(std::size_t i, void* user);

    // Submit one task against `c`. Serial backend (S0): runs `fn(user)` inline
    // before returning. Pool backend (S1+): may run on a worker thread.
    void run(Counter& c, TaskFn fn, void* user);

    // Submit `n` index tasks against `c`: fn(i, user) for i in [0, n). Serial
    // backend (S0): runs them in order, inline, before returning. n == 0 is a
    // no-op.
    void run(Counter& c, std::size_t n, IndexFn fn, void* user);

    // Block until `c` reaches zero. Serial backend (S0): immediate -- every task
    // ran inline during run(). Pool backend: the OUTER wait (the caller is not a
    // worker) blocks; a NESTED wait from inside a worker task parks the calling
    // fibre and steals other work until the counter drains (S2). Calling wait()
    // from a worker before fibres exist (S1) is not supported -- see #293's
    // wait-contract: S1 fans out only flat work, whose tasks never nest-wait.
    void wait(Counter& c);

    // A fork-join completion counter: the number of tasks submitted against it but
    // not yet finished. Lives on the caller's stack, spanning one run()/wait()
    // pair; non-copyable and non-movable (a worker may hold a pointer to it).
    //
    // At S0 the count is only ever transiently non-zero inside run() (tasks finish
    // inline), so it is a plain int; it becomes atomic when the worker pool lands
    // (S1). The type exists now so consumer code is written against the final shape
    // from the first commit.
    class Counter
    {
    public:
        Counter() = default;

        Counter(const Counter&) = delete;
        Counter& operator=(const Counter&) = delete;
        Counter(Counter&&) = delete;
        Counter& operator=(Counter&&) = delete;

    private:
        // Outstanding-task count. Plain int at S0 (single thread, drained inline);
        // becomes std::atomic when the pool makes completion cross-thread (S1).
        int outstanding_ = 0;

        friend void run(Counter&, TaskFn, void*);
        friend void run(Counter&, std::size_t, IndexFn, void*);
        friend void wait(Counter&);
    };
}
