#include <tasks/task_scheduler.h>

#include <optional>
#include <utility>

namespace wz::tasks
{
    namespace
    {
        // -1 on the main/engine thread (and any non-pool thread); >= 0 on a worker
        // (its deque index). run()/wait() read this to keep nested work serial.
        thread_local int t_worker_index = -1;

        std::atomic<TaskScheduler*> g_scheduler{ nullptr };

        unsigned resolve_worker_count(unsigned requested)
        {
            if (requested != 0)
                return requested;
            const unsigned hc = std::thread::hardware_concurrency();
            const unsigned leave_for_others = 2;  // main + logging/audio
            const unsigned n = (hc > leave_for_others) ? hc - leave_for_others : 1;
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

    TaskScheduler::TaskScheduler(unsigned num_workers)
    {
        const unsigned n = resolve_worker_count(num_workers);
        deques_.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            deques_.push_back(
                std::make_unique<WorkStealingDeque<Task>>());

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
    }

    void TaskScheduler::submit(const Task& t)
    {
        const unsigned n = static_cast<unsigned>(deques_.size());
        const unsigned pick =
            next_victim_.fetch_add(1, std::memory_order_relaxed) % n;
        deques_[pick]->push(t);
        // Push is visible (deque mutex); wake one parked worker. The lock closes
        // the check-then-park race: a worker between any_work()==false and
        // cv_.wait() holds mutex_, so this notify cannot be lost.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cv_.notify_one();
        }
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

    bool TaskScheduler::any_work() const
    {
        for (const std::unique_ptr<WorkStealingDeque<Task>>& d : deques_)
            if (!d->empty())
                return true;
        return false;
    }

    void TaskScheduler::execute(const Task& t)
    {
        if (t.task_fn != nullptr)
            t.task_fn(t.user);
        else
            t.index_fn(t.index, t.user);

        // Completion publishes the task's writes and, on reaching zero, wakes the
        // thread blocked in wait(). acq_rel chains the per-task releases so the
        // waiter's acquire sees every task's effects, not just the last.
        if (t.counter->outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1)
            t.counter->outstanding_.notify_all();
    }

    void TaskScheduler::worker_main(unsigned index)
    {
        t_worker_index = static_cast<int>(index);

        while (running_.load(std::memory_order_acquire))
        {
            Task t;
            if (pop_or_steal(index, t))
            {
                execute(t);
                continue;
            }

            // Nothing found; spin briefly before parking (absorbs tiny gaps
            // between a steal miss and fresh work without a syscall).
            bool found = false;
            for (int s = 0; s < 64; ++s)
            {
                std::this_thread::yield();
                if (pop_or_steal(index, t))
                {
                    found = true;
                    break;
                }
            }
            if (found)
            {
                execute(t);
                continue;
            }

            std::unique_lock<std::mutex> lock(mutex_);
            if (!running_.load(std::memory_order_acquire))
                break;
            if (any_work())  // re-check under the lock: no lost wakeup
                continue;
            cv_.wait(lock);
        }

        t_worker_index = -1;
    }
}
