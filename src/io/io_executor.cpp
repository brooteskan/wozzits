#include <io/io_executor.h>

#include <utility>

// The IO lane (#305 step 4a). Each thread loops: wait for a closure, run it
// OUTSIDE the lock (it blocks on disk), repeat -- until teardown drains the queue
// and stops it. See io_executor.h for why this is dedicated blocking threads with
// a plain mutex queue rather than the lock-free compute pool.

namespace wz::io
{
    namespace
    {
        // Small blocking-I/O group: enough to overlap a stalled write with other
        // I/O, not so many that idle threads pile up (I/O is throughput-bound).
        unsigned resolve_threads(unsigned requested)
        {
            return requested != 0 ? requested : 2u;
        }
    }

    IoExecutor::IoExecutor(unsigned threads)
    {
        const unsigned n = resolve_threads(threads);
        threads_.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            threads_.emplace_back([this] { worker_main(); });
    }

    IoExecutor::~IoExecutor()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;  // stop accepting; workers still drain what's queued
        }
        cv_.notify_all();
        for (std::thread& t : threads_)
            t.join();
    }

    void IoExecutor::post(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_)
                return;  // tearing down -- drop (see the destructor contract)
            queue_.push_back(std::move(job));
        }
        cv_.notify_one();
    }

    void IoExecutor::worker_main()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
                // Drain-on-teardown: only exit once running_ is false AND the queue
                // is empty, so every posted closure runs before the thread leaves.
                if (queue_.empty())
                    return;
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            job();  // runs outside the lock: it blocks on I/O
        }
    }
}
