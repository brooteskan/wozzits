#pragma once

// tasks/task_scheduler.h
//
// The worker pool behind wz::tasks (#293). Installed globally (set/get, like
// wz::get_async_executor); with none installed, run()/wait() fall back to the
// serial-inline S0 backend, so existing callers and the S3 force-serial path are
// unchanged.
//
// Model (the #293 "safe island"): the engine/main thread submits work and BLOCKS
// in wait() while the pool's workers drain it -- the main thread never becomes a
// worker or a fibre. Each worker thread is a SCHEDULER FIBRE running the dispatch
// loop; each task runs on a TASK FIBRE drawn from a pool. When a task fibre blocks
// in wait() (S2), it PARKS -- switches back to the scheduler, which steals other
// work -- and is resumed when its counter drains, so no worker ever blocks and the
// pool is deadlock-free. Because a worker's run() submits (rather than running
// inline) and its wait() parks, the recursion itself parallelizes, not just the
// flat top level.

#include <tasks/task.h>
#include <tasks/fiber_backend.h>
#include <containers/work_stealing_deque.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace wz::tasks
{
    using wz::core::containers::WorkStealingDeque;

    // One unit of work + the counter it completes. Exactly one of task_fn /
    // index_fn is set (index_fn also reads `index`).
    struct Task
    {
        Counter*    counter  = nullptr;
        void*       user     = nullptr;
        TaskFn      task_fn  = nullptr;
        IndexFn     index_fn = nullptr;
        std::size_t index    = 0;
    };

    struct FiberContext;  // one task fibre; defined in the .cpp

    class TaskScheduler
    {
    public:
        // num_workers == 0 picks min(16, hardware_concurrency - 2), >= 1.
        explicit TaskScheduler(unsigned num_workers = 0);
        ~TaskScheduler();

        TaskScheduler(const TaskScheduler&) = delete;
        TaskScheduler& operator=(const TaskScheduler&) = delete;

        unsigned worker_count() const { return static_cast<unsigned>(deques_.size()); }

        // Called by run()/wait(). submit: enqueue a task (a worker enqueues onto its
        // own deque, the main thread round-robins). wait_on_worker: park the running
        // task fibre on `c`, doing other work until it drains.
        void submit(const Task& t);
        void wait_on_worker(Counter& c);

    private:
        // Address space reserved per task fibre stack (committed lazily).
        static constexpr std::size_t kFiberStackBytes = 512 * 1024;

        void worker_main(unsigned index);
        bool try_run_one(unsigned index);
        void run_fiber(FiberContext* fc);
        void park_current_fiber(Counter& c);
        void execute_task(const Task& t);
        void complete(Counter& c);

        bool pop_or_steal(unsigned index, Task& out);
        bool has_any_work();

        FiberContext* acquire_fiber();
        void release_fiber(FiberContext* fc);
        void push_resumable(FiberContext* fc);
        FiberContext* pop_resumable();

        // A task fibre's entry: run its assigned task, yield back "finished". A
        // static member so its address is a plain FiberEntry and it can reach the
        // private members. Never returns.
        static void task_fiber_entry(void* arg);

        std::vector<std::unique_ptr<WorkStealingDeque<Task>>> deques_;
        std::vector<std::thread> workers_;

        std::mutex mutex_;                       // cv park/wake + resumable_ queue
        std::condition_variable cv_;
        std::deque<FiberContext*> resumable_;    // guarded by mutex_
        std::atomic<bool> running_{ true };
        std::atomic<unsigned> next_victim_{ 0 };  // main-thread round-robin target

        std::mutex fiber_mutex_;                 // the fibre pool
        std::vector<std::unique_ptr<FiberContext>> all_fibers_;  // guarded by fiber_mutex_
        std::vector<FiberContext*> free_fibers_;                 // guarded by fiber_mutex_
    };

    // Install / query the global scheduler. The caller retains ownership; it must
    // outlive every run()/wait() that could dispatch to it, and must not be
    // destroyed with work in flight. Null (the default) is the serial-inline backend.
    void set_task_scheduler(TaskScheduler* scheduler);
    TaskScheduler* get_task_scheduler();

    // True iff the calling thread is a pool worker (so run()/wait() take the
    // submit/park paths rather than block).
    bool is_worker_thread();
}
