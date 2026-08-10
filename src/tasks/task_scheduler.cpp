#include <tasks/task_scheduler.h>

#include <tasks/fiber_backend.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

// Marks the TLS accessors below: forces the thread_local base to be recomputed on
// the executing thread rather than hoisted across a fibre switch that changes
// threads (see the accessor note in the anonymous namespace).
#if defined(_MSC_VER)
#define WZ_NOINLINE __declspec(noinline)
#else
#define WZ_NOINLINE __attribute__((noinline))
#endif

// wz::tasks worker pool (#293, S1 pool + S2 fibres + S3 diagnostics).
//
// Each worker thread converts itself to a SCHEDULER FIBRE and loops: run a
// resumable parked fibre if any, else run a fresh task on a pooled TASK FIBRE,
// else spin briefly and park on the cv. A task fibre runs its task; if the task
// blocks in wait(), it PARKS (switches back to the scheduler) and is registered on
// its counter; when that counter hits zero the completer makes it resumable and
// some worker switches back into it.
//
// The one race the design turns on: a parked fibre must not become resumable until
// it has fully switched off its stack, or another worker could resume it mid-
// switch and corrupt its stack. So the task fibre only RECORDS its park request and
// switches away; the SCHEDULER (running only after the switch completed) registers
// it on the counter, and the completer claims it. The registration CAS and the
// completion exchange serialize on Counter::waiter_, so exactly one side makes the
// fibre resumable, exactly once.

namespace wz::tasks
{
    // One task fibre. Pooled and reused across tasks.
    struct FiberContext
    {
        Fiber fiber;
        Task task;
        TaskScheduler* scheduler = nullptr;
    };

    namespace
    {
        enum class YieldReason { None, Finished, Parking };

        // Per-WORKER-THREAD state. thread_local, NOT fibre-local: a migrated fibre
        // reads the state of whichever thread now runs it -- which is what we want.
        // But that only holds if each access re-reads the TLS base on the executing
        // thread; the migration-sensitive ones go through the tls_* accessors below
        // (see their note) so the optimizer cannot hoist the base across a switch.
        thread_local int t_worker_index = -1;
        thread_local Fiber t_scheduler_fiber{};
        thread_local FiberContext* t_current_fiber = nullptr;

        thread_local YieldReason t_yield_reason = YieldReason::None;
        thread_local FiberContext* t_yield_fiber = nullptr;
        thread_local Counter* t_yield_counter = nullptr;

        std::atomic<TaskScheduler*> g_scheduler{ nullptr };
        std::atomic<bool> g_force_serial{ false };

        // A unique address used as Counter::waiter_'s "already reached zero"
        // sentinel; never equal to a real FiberContext*.
        char g_done_storage;
        void* const kDone = &g_done_storage;

        unsigned resolve_worker_count(unsigned requested)
        {
            if (requested != 0)
                return requested;
            const unsigned hc = std::thread::hardware_concurrency();
            const unsigned n = (hc > 2) ? hc - 2 : 1;
            return (n < 16u) ? n : 16u;
        }

        // A task fibre can PARK on one worker and RESUME on another (the pool
        // migrates it when its counter drains). After such a switch, its TLS reads
        // must resolve to the thread NOW running it. The optimizer, assuming a
        // thread cannot change mid-function, hoists the loop-invariant TLS-base
        // computation out of task_fiber_entry's for(;;) loop -- computing it once on
        // the thread that first ran the fibre and reusing it after execute_task has
        // migrated the fibre elsewhere. The stale base reaches the ORIGINAL thread's
        // slots, so a fibre "switching back to its scheduler" jumps into a DIFFERENT
        // thread's scheduler fibre; two threads then run one fibre stack and smash
        // it -- a release-only crash (an -O0 build reloads TLS every access and hides
        // it). These WZ_NOINLINE accessors are opaque calls the compiler cannot hoist
        // out of the loop, so the base is recomputed on the executing thread at use.
        WZ_NOINLINE Fiber tls_scheduler_fiber() { return t_scheduler_fiber; }
        WZ_NOINLINE FiberContext* tls_current_fiber() { return t_current_fiber; }
        WZ_NOINLINE void tls_store_yield(
            YieldReason reason, FiberContext* fiber, Counter* counter)
        {
            t_yield_reason = reason;
            t_yield_fiber = fiber;
            t_yield_counter = counter;
        }
    }

    void set_task_scheduler(TaskScheduler* scheduler)
    {
        g_scheduler.store(scheduler, std::memory_order_release);
    }

    TaskScheduler* get_task_scheduler()
    {
        return g_scheduler.load(std::memory_order_acquire);
    }

    bool is_worker_thread()
    {
        return t_worker_index >= 0;
    }

    void set_force_serial(bool force)
    {
        g_force_serial.store(force, std::memory_order_release);
    }

    bool force_serial()
    {
        // Read the env var once (thread-safe static init); OR the programmatic flag.
        static const bool from_env = []
        {
#pragma warning(push)
#pragma warning(disable : 4996)  // std::getenv is the correct, portable call
            const char* v = std::getenv("WZ_TASKS_FORCE_SERIAL");
#pragma warning(pop)
            return v != nullptr && v[0] != '\0' && v[0] != '0';
        }();
        return from_env || g_force_serial.load(std::memory_order_acquire);
    }

    void TaskScheduler::task_fiber_entry(void* arg)
    {
        FiberContext* self = static_cast<FiberContext*>(arg);
        for (;;)
        {
            self->scheduler->execute_task(self->task);
            // execute_task may have MIGRATED this fibre to another worker (a nested
            // wait parks it; a different worker resumes it). `self` is the fibre's
            // own entry arg so it is always right, but the yield state and the
            // scheduler-fibre handle are thread_local and must be THIS thread's --
            // hence the accessors (see their note).
            tls_store_yield(YieldReason::Finished, self, nullptr);
            switch_to_fiber(tls_scheduler_fiber());
        }
    }

    TaskScheduler::TaskScheduler(
        unsigned num_workers,
        std::chrono::milliseconds stall_threshold,
        std::chrono::milliseconds stall_interval)
        : stall_threshold_(stall_threshold), stall_interval_(stall_interval)
    {
        const unsigned n = resolve_worker_count(num_workers);
        deques_.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            deques_.push_back(std::make_unique<WorkStealingDeque<Task>>());

        workers_.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            workers_.emplace_back([this, i] { worker_main(i); });

        watchdog_ = std::thread([this] { watchdog_main(); });
    }

    TaskScheduler::~TaskScheduler()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_.store(false, std::memory_order_release);
            cv_.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock(watchdog_mutex_);
            watchdog_cv_.notify_all();
        }
        for (std::thread& w : workers_)
            w.join();
        if (watchdog_.joinable())
            watchdog_.join();

        // Destruction contract: no work in flight, so every fibre is free and
        // suspended at its entry-loop switch-back. Safe to delete.
        for (std::unique_ptr<FiberContext>& fc : all_fibers_)
            destroy_fiber(fc->fiber);
    }

    void TaskScheduler::submit(const Task& t)
    {
        // A worker OWNS its deque, so it pushes there directly -- locality: its own
        // freshly-forked children stay on its LIFO. A non-worker (the safe-island
        // main thread) owns no deque and cannot push into one (push/pop are
        // owner-only), so it hands the task off through the MPSC injection queue; a
        // worker drains that into its deque in drain_injection().
        if (t_worker_index >= 0)
            deques_[static_cast<unsigned>(t_worker_index)]->push(t);
        else
            injection_.push(t);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cv_.notify_one();
        }
    }

    FiberContext* TaskScheduler::acquire_fiber()
    {
        std::lock_guard<std::mutex> lock(fiber_mutex_);
        if (!free_fibers_.empty())
        {
            FiberContext* fc = free_fibers_.back();
            free_fibers_.pop_back();
            return fc;
        }
        auto owned = std::make_unique<FiberContext>();
        FiberContext* fc = owned.get();
        fc->scheduler = this;
        fc->fiber = create_fiber(kFiberStackBytes, &TaskScheduler::task_fiber_entry, fc);
        all_fibers_.push_back(std::move(owned));
        return fc;
    }

    void TaskScheduler::release_fiber(FiberContext* fc)
    {
        std::lock_guard<std::mutex> lock(fiber_mutex_);
        free_fibers_.push_back(fc);
    }

    void TaskScheduler::push_resumable(FiberContext* fc)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resumable_.push_back(fc);
        cv_.notify_one();
    }

    FiberContext* TaskScheduler::pop_resumable()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (resumable_.empty())
            return nullptr;
        FiberContext* fc = resumable_.front();
        resumable_.pop_front();
        return fc;
    }

    void TaskScheduler::drain_injection(unsigned index)
    {
        // The injection queue is MPSC: many submitters, but only ONE consumer at a
        // time. The token enforces that single-consumer side -- a worker that fails
        // to acquire it leaves draining to whoever holds it and moves on to pop/steal
        // (the drained tasks land in the drainer's deque and are then stealable). The
        // empty() pre-check is the cheap no-CAS fast path for the common "nothing
        // injected" case; it may miss an in-flight push, which the submit's notify
        // and the next loop iteration catch.
        if (injection_.empty())
            return;
        bool expected = false;
        if (!injection_drain_lock_.compare_exchange_strong(
                expected, true, std::memory_order_acquire, std::memory_order_relaxed))
            return;
        Task t;
        while (injection_.try_pop(t))
            deques_[index]->push(t);  // owner push: this worker owns deque `index`
        injection_drain_lock_.store(false, std::memory_order_release);
    }

    bool TaskScheduler::pop_or_steal(unsigned index, Task& out)
    {
        drain_injection(index);
        if (std::optional<Task> t = deques_[index]->pop())
        {
            out = *t;
            return true;
        }
        const unsigned n = static_cast<unsigned>(deques_.size());
        for (unsigned i = 1; i < n; ++i)
        {
            const unsigned victim = (index + i) % n;
            if (std::optional<Task> t = deques_[victim]->steal())
            {
                out = *t;
                return true;
            }
        }
        return false;
    }

    bool TaskScheduler::has_any_work()
    {
        if (!resumable_.empty())
            return true;
        if (!injection_.empty())
            return true;
        for (const std::unique_ptr<WorkStealingDeque<Task>>& d : deques_)
            if (!d->empty())
                return true;
        return false;
    }

    void TaskScheduler::execute_task(const Task& t)
    {
        if (t.task_fn != nullptr)
            t.task_fn(t.user);
        else
            t.index_fn(t.index, t.user);
        complete(*t.counter);
    }

    void TaskScheduler::complete(Counter& c)
    {
        // Progress signal for the watchdog (a plain monotonic tick).
        completed_count_.fetch_add(1, std::memory_order_relaxed);

        if (c.outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            c.outstanding_.notify_all();  // wake a main-thread waiter (no-op if none)
            void* w = c.waiter_.exchange(kDone, std::memory_order_acq_rel);
            if (w != nullptr && w != kDone)
                push_resumable(static_cast<FiberContext*>(w));
        }
    }

    void TaskScheduler::park_current_fiber(Counter& c)
    {
        // Runs on the task fibre, which may already have migrated across workers, so
        // every TLS access here goes through the accessors (see their note): the
        // fibre to register is THIS thread's current fibre, and the switch target is
        // THIS thread's scheduler fibre.
        tls_store_yield(YieldReason::Parking, tls_current_fiber(), &c);
        switch_to_fiber(tls_scheduler_fiber());
    }

    void TaskScheduler::wait_on_worker(Counter& c)
    {
        while (c.outstanding_.load(std::memory_order_acquire) != 0)
            park_current_fiber(c);
    }

    void TaskScheduler::wait_on_main(Counter& c)
    {
        active_waits_.fetch_add(1, std::memory_order_relaxed);
        for (;;)
        {
            const int v = c.outstanding_.load(std::memory_order_acquire);
            if (v == 0)
                break;
            c.outstanding_.wait(v, std::memory_order_acquire);
        }
        active_waits_.fetch_sub(1, std::memory_order_relaxed);
    }

    void TaskScheduler::run_fiber(FiberContext* fc)
    {
        t_current_fiber = fc;
        switch_to_fiber(fc->fiber);
        t_current_fiber = nullptr;

        const YieldReason reason = t_yield_reason;
        FiberContext* yf = t_yield_fiber;
        Counter* yc = t_yield_counter;
        t_yield_reason = YieldReason::None;
        t_yield_fiber = nullptr;
        t_yield_counter = nullptr;

        if (reason == YieldReason::Finished)
        {
            release_fiber(yf);
        }
        else if (reason == YieldReason::Parking)
        {
            void* expected = nullptr;
            if (!yc->waiter_.compare_exchange_strong(
                    expected, yf, std::memory_order_acq_rel))
            {
                // expected == kDone: yc already reached zero -> resume yf now.
                push_resumable(yf);
            }
        }
    }

    bool TaskScheduler::try_run_one(unsigned index)
    {
        if (FiberContext* rf = pop_resumable())
        {
            run_fiber(rf);
            return true;
        }
        Task t;
        if (pop_or_steal(index, t))
        {
            FiberContext* fc = acquire_fiber();
            fc->task = t;
            run_fiber(fc);
            return true;
        }
        return false;
    }

    void TaskScheduler::worker_main(unsigned index)
    {
        t_worker_index = static_cast<int>(index);
        t_scheduler_fiber = convert_thread_to_fiber();

        while (running_.load(std::memory_order_acquire))
        {
            if (try_run_one(index))
                continue;

            bool found = false;
            for (int s = 0; s < 64; ++s)
            {
                std::this_thread::yield();
                if (try_run_one(index))
                {
                    found = true;
                    break;
                }
            }
            if (found)
                continue;

            std::unique_lock<std::mutex> lock(mutex_);
            if (!running_.load(std::memory_order_acquire))
                break;
            if (has_any_work())
                continue;
            cv_.wait(lock);
        }

        convert_fiber_to_thread();
        t_scheduler_fiber = Fiber{};
        t_worker_index = -1;
    }

    void TaskScheduler::set_stall_callback(std::function<void()> callback)
    {
        std::lock_guard<std::mutex> lock(watchdog_mutex_);
        stall_callback_ = std::move(callback);
    }

    void TaskScheduler::watchdog_main()
    {
        // A stall is a main-thread wait outstanding while NO task completes for
        // stall_threshold_. That is a lost task / deadlock -- the safe-island thread
        // would otherwise block forever. Report once per episode.
        std::uint64_t last_completed = completed_count_.load(std::memory_order_relaxed);
        std::chrono::milliseconds stalled_for{ 0 };
        bool reported = false;

        std::unique_lock<std::mutex> lock(watchdog_mutex_);
        while (running_.load(std::memory_order_acquire))
        {
            watchdog_cv_.wait_for(lock, stall_interval_);
            if (!running_.load(std::memory_order_acquire))
                break;

            const std::uint64_t now = completed_count_.load(std::memory_order_relaxed);
            const bool waiting = active_waits_.load(std::memory_order_relaxed) > 0;

            if (waiting && now == last_completed)
            {
                stalled_for += stall_interval_;
                if (stalled_for >= stall_threshold_ && !reported)
                {
                    reported = true;
                    if (stall_callback_)
                    {
                        std::function<void()> cb = stall_callback_;
                        lock.unlock();
                        cb();
                        lock.lock();
                    }
                    else
                    {
                        std::fprintf(stderr,
                            "wz::tasks: STALL -- a wait has not made progress in "
                            "%lld ms; a task may be lost or deadlocked.\n",
                            static_cast<long long>(stalled_for.count()));
                    }
                }
            }
            else
            {
                stalled_for = std::chrono::milliseconds{ 0 };
                reported = false;
            }
            last_completed = now;
        }
    }
}
