#pragma once

// tasks/task_scheduler.h
//
// The S1 worker pool behind wz::tasks (#293). Installed globally (set/get, like
// wz::get_async_executor); with none installed, run()/wait() fall back to the
// serial-inline S0 backend, so existing callers and the S3 force-serial path are
// unchanged.
//
// Model (the #293 "safe island"): the engine/main thread submits work and BLOCKS
// in wait() while the pool's workers drain it -- the main thread never becomes a
// worker. Each worker owns a work-stealing deque (containers/): it runs its own
// LIFO and steals FIFO from others when idle. run()/wait() called NESTED from a
// worker run inline-serial (S1 keeps recursion on the worker's own stack; fibres
// unlock the parallel nested wait at S2), so no worker ever blocks on a counter
// and the pool is deadlock-free by construction.

#include <tasks/task.h>
#include <containers/work_stealing_deque.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace wz::tasks
{
    using wz::core::containers::WorkStealingDeque;

    // One unit of work + the counter it completes. Trivially copyable: exactly one
    // of task_fn / index_fn is set (index_fn also reads `index`).
    struct Task
    {
        Counter*    counter  = nullptr;
        void*       user     = nullptr;
        TaskFn      task_fn  = nullptr;
        IndexFn     index_fn = nullptr;
        std::size_t index    = 0;
    };

    class TaskScheduler
    {
    public:
        // num_workers == 0 picks a default that leaves the main thread and the
        // logging/audio threads room: min(16, hardware_concurrency - 2), >= 1.
        explicit TaskScheduler(unsigned num_workers = 0);
        ~TaskScheduler();

        TaskScheduler(const TaskScheduler&) = delete;
        TaskScheduler& operator=(const TaskScheduler&) = delete;

        unsigned worker_count() const
        {
            return static_cast<unsigned>(deques_.size());
        }

        // Called by run() on a non-worker thread: round-robin a task onto a worker
        // deque and wake a parked worker.
        void submit(const Task& t);

    private:
        void worker_main(unsigned index);
        bool pop_or_steal(unsigned index, Task& out);
        bool any_work() const;
        void execute(const Task& t);  // run the fn, then complete its counter

        std::vector<std::unique_ptr<WorkStealingDeque<Task>>> deques_;
        std::vector<std::thread> workers_;

        std::mutex mutex_;               // guards the park/wake handshake
        std::condition_variable cv_;
        std::atomic<bool> running_{ true };
        std::atomic<unsigned> next_victim_{ 0 };  // round-robin submit target
    };

    // Install / query the global scheduler. The caller retains ownership; it must
    // outlive every run()/wait() that could dispatch to it. Null (the default) is
    // the serial-inline backend.
    void set_task_scheduler(TaskScheduler* scheduler);
    TaskScheduler* get_task_scheduler();

    // True iff the calling thread is a pool worker (a nested context), so run()/
    // wait() keep it serial.
    bool is_worker_thread();
}
