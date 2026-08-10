#pragma once

// io/io_executor.h
//
// The IO lane (#305 step 4a): a small group of DEDICATED BLOCKING threads that
// run fire-and-forget I/O closures off the frame and compute threads. It is the
// concrete backend for the wz::IAsyncExecutor seam (async/async.h) -- engine init
// installs one via set_async_executor, which activates wz::fs::async_read_file /
// async_write_file (both are inert until an executor exists, returning IOError).
//
// Why its OWN threads, not the wz::tasks compute pool (#305 decision 2): a
// synchronous write_file blocks in the kernel, where the work-stealing pool's
// invariant -- "a worker never blocks; a wait parks the fibre" -- does not hold.
// Blocking a compute worker on disk reintroduces head-of-line stalls and starves
// compute during a big save. So I/O gets throughput-bound blocking threads of its
// own; the compute pool stays core-bound.
//
// The queue is a plain mutex + condition_variable + deque, deliberately: I/O is
// the occasional slow verb (a scene save), not a hot path, so the lock-free
// machinery the compute deque needs (#304) would be misplaced here -- the lock is
// only ever contended by an idle-blocked poster and the draining threads.

#include <async/async.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace wz::io
{
    class IoExecutor final : public wz::IAsyncExecutor
    {
    public:
        // threads == 0 picks a small default. The blocking-I/O group is
        // throughput-bound, not core-bound: a couple of threads let one stalled
        // write overlap other I/O without oversubscribing cores.
        explicit IoExecutor(unsigned threads = 0);

        // Stops accepting new work, DRAINS everything already queued, then joins.
        // A closure posted before shutdown always runs -- a scene save handed off
        // just before teardown is never silently dropped.
        ~IoExecutor() override;

        IoExecutor(const IoExecutor&) = delete;
        IoExecutor& operator=(const IoExecutor&) = delete;

        // Enqueue a closure to run on an I/O thread. Returns immediately -- never
        // blocks on the I/O itself. Safe to call from any thread. A post once
        // teardown has begun is dropped (the executor is going away).
        void post(std::function<void()> job) override;

        unsigned thread_count() const
        {
            return static_cast<unsigned>(threads_.size());
        }

    private:
        void worker_main();

        std::mutex mutex_;                            // guards queue_ + running_
        std::condition_variable cv_;
        std::deque<std::function<void()>> queue_;
        bool running_ = true;
        std::vector<std::thread> threads_;
    };
}
