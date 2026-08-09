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
// loop; each task runs on a TASK FIBRE from a pool. When a task fibre blocks in
// wait() (S2), it PARKS -- switches back to the scheduler, which steals other
// work -- and is resumed when its counter drains, so no worker ever blocks and the
// pool is deadlock-free.
//
// DETERMINISM (S3, locked): results are independent of the schedule. A reduction's
// order is fixed by the data (the contraction folds a node's children in CSR
// order regardless of which finishes first), and FIBER_FLAG_FLOAT_SWITCH preserves
// FP state across a switch, so a computation run on the pool is BIT-IDENTICAL to
// the serial backend and to any worker count -- pinned by the cognition oracle.
//
// DIAGNOSTICS (S3): a watchdog detects a counter that never drains (a lost task /
// deadlock would otherwise hang the safe-island main thread forever); and
// set_force_serial / the WZ_TASKS_FORCE_SERIAL env var bypass the pool so a bug can
// be bisected serial-vs-parallel without recompiling.

#include <tasks/task.h>
#include <tasks/fiber_backend.h>
#include <containers/work_stealing_deque.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
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
        // num_workers == 0 picks min(16, hardware_concurrency - 2), >= 1. The
        // watchdog reports a stall when a wait has been outstanding this long with
        // no task completing; it samples every `stall_interval`.
        explicit TaskScheduler(
            unsigned num_workers = 0,
            std::chrono::milliseconds stall_threshold = std::chrono::seconds(5),
            std::chrono::milliseconds stall_interval = std::chrono::seconds(1));
        ~TaskScheduler();

        TaskScheduler(const TaskScheduler&) = delete;
        TaskScheduler& operator=(const TaskScheduler&) = delete;

        unsigned worker_count() const { return static_cast<unsigned>(deques_.size()); }

        // run()/wait() entry points. submit: enqueue a task. wait_on_worker: park
        // the running task fibre until `c` drains. wait_on_main: block the safe-
        // island thread on `c` (with stall accounting).
        void submit(const Task& t);
        void wait_on_worker(Counter& c);
        void wait_on_main(Counter& c);

        // Replace the stall action (default: a one-line message to stderr). Set
        // before work runs. Invoked on the watchdog thread.
        void set_stall_callback(std::function<void()> callback);

    private:
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

        void watchdog_main();

        static void task_fiber_entry(void* arg);

        std::vector<std::unique_ptr<WorkStealingDeque<Task>>> deques_;
        std::vector<std::thread> workers_;

        std::mutex mutex_;                       // cv park/wake + resumable_ queue
        std::condition_variable cv_;
        std::deque<FiberContext*> resumable_;    // guarded by mutex_
        std::atomic<bool> running_{ true };
        std::atomic<unsigned> next_victim_{ 0 };

        std::mutex fiber_mutex_;                 // the fibre pool
        std::vector<std::unique_ptr<FiberContext>> all_fibers_;  // guarded by fiber_mutex_
        std::vector<FiberContext*> free_fibers_;                 // guarded by fiber_mutex_

        // ─── Stall watchdog (S3) ────────────────────────────────────────────────
        std::thread watchdog_;
        std::condition_variable watchdog_cv_;
        std::mutex watchdog_mutex_;              // guards stall_callback_
        std::function<void()> stall_callback_;
        std::atomic<std::uint64_t> completed_count_{ 0 };  // progress signal
        std::atomic<int> active_waits_{ 0 };               // blocking main-thread waits
        const std::chrono::milliseconds stall_threshold_;
        const std::chrono::milliseconds stall_interval_;
    };

    // Install / query the global scheduler. The caller retains ownership; it must
    // outlive every run()/wait() that could dispatch to it, and must not be
    // destroyed with work in flight. Null (the default) is the serial-inline backend.
    void set_task_scheduler(TaskScheduler* scheduler);
    TaskScheduler* get_task_scheduler();

    // True iff the calling thread is a pool worker.
    bool is_worker_thread();

    // Force the serial S0 backend even when a scheduler is installed (S3 debug
    // flag): run() runs inline and wait() is immediate, so a bug can be bisected
    // serial-vs-parallel. Also enabled by the WZ_TASKS_FORCE_SERIAL env var.
    void set_force_serial(bool force);
    bool force_serial();
}
