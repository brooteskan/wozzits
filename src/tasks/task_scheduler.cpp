#include <tasks/task_scheduler.h>

#include <tasks/fiber_backend.h>

#include <optional>
#include <utility>

// wz::tasks worker pool (#293, S1 pool + S2 fibres).
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
        // reads the state of whichever thread now runs it -- which is what we want
        // (its scheduler fibre, its worker index). The values that must survive a
        // park/resume live on the fibre's own stack or in the Counter, not here.
        thread_local int t_worker_index = -1;
        thread_local Fiber t_scheduler_fiber{};            // this thread's dispatch fibre
        thread_local FiberContext* t_current_fiber = nullptr;  // task fibre running now

        // Set by a task fibre immediately before switching to the scheduler, read by
        // the scheduler immediately after -- same thread, so no migration between.
        thread_local YieldReason t_yield_reason = YieldReason::None;
        thread_local FiberContext* t_yield_fiber = nullptr;
        thread_local Counter* t_yield_counter = nullptr;

        std::atomic<TaskScheduler*> g_scheduler{ nullptr };

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

    void TaskScheduler::task_fiber_entry(void* arg)
    {
        FiberContext* self = static_cast<FiberContext*>(arg);
        for (;;)
        {
            self->scheduler->execute_task(self->task);
            // Task done: ask the scheduler to recycle this fibre, then switch back.
            t_yield_reason = YieldReason::Finished;
            t_yield_fiber = self;
            switch_to_fiber(t_scheduler_fiber);
            // Reused: the scheduler set self->task + t_current_fiber = self and
            // switched to us; loop to run the new task.
        }
    }

    TaskScheduler::TaskScheduler(unsigned num_workers)
    {
        const unsigned n = resolve_worker_count(num_workers);
        deques_.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            deques_.push_back(std::make_unique<WorkStealingDeque<Task>>());

        workers_.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            workers_.emplace_back([this, i] { worker_main(i); });
    }

    TaskScheduler::~TaskScheduler()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_.store(false, std::memory_order_release);
            cv_.notify_all();
        }
        for (std::thread& w : workers_)
            w.join();

        // Destruction contract: no work in flight, so every fibre is free and
        // suspended at its entry-loop switch-back. Safe to delete.
        for (std::unique_ptr<FiberContext>& fc : all_fibers_)
            destroy_fiber(fc->fiber);
    }

    void TaskScheduler::submit(const Task& t)
    {
        const unsigned n = static_cast<unsigned>(deques_.size());
        unsigned pick;
        if (t_worker_index >= 0)
            pick = static_cast<unsigned>(t_worker_index);  // own deque (locality)
        else
            pick = next_victim_.fetch_add(1, std::memory_order_relaxed) % n;
        deques_[pick]->push(t);
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

    bool TaskScheduler::pop_or_steal(unsigned index, Task& out)
    {
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
        // Called while holding mutex_ (park decision). resumable_ is guarded by
        // mutex_; the deques by their own mutexes (lock order mutex_ ⊃ deque, and
        // submit never holds both simultaneously, so there is no cycle).
        if (!resumable_.empty())
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
        if (c.outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            // Count hit zero. Wake a main-thread waiter (a no-op if none).
            c.outstanding_.notify_all();
            // Claim a parked fibre waiter, if one is registered.
            void* w = c.waiter_.exchange(kDone, std::memory_order_acq_rel);
            if (w != nullptr && w != kDone)
                push_resumable(static_cast<FiberContext*>(w));
        }
    }

    void TaskScheduler::park_current_fiber(Counter& c)
    {
        // Only RECORD the request and switch away; the scheduler registers us on c
        // (after this switch completes) so we cannot be resumed mid-switch.
        t_yield_reason = YieldReason::Parking;
        t_yield_fiber = t_current_fiber;
        t_yield_counter = &c;
        switch_to_fiber(t_scheduler_fiber);
        // Resumed after c reached zero; return to wait_on_worker's re-check loop.
    }

    void TaskScheduler::wait_on_worker(Counter& c)
    {
        while (c.outstanding_.load(std::memory_order_acquire) != 0)
            park_current_fiber(c);
    }

    void TaskScheduler::run_fiber(FiberContext* fc)
    {
        t_current_fiber = fc;
        switch_to_fiber(fc->fiber);  // run/resume until it yields back
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
            // Register yf as yc's waiter. Safe to make resumable only from here on,
            // because yf has fully switched off its stack.
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

            // Nothing found; spin briefly before parking (absorbs tiny gaps).
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
            if (has_any_work())  // re-check under the lock: no lost wakeup
                continue;
            cv_.wait(lock);
        }

        convert_fiber_to_thread();
        t_scheduler_fiber = Fiber{};
        t_worker_index = -1;
    }
}
