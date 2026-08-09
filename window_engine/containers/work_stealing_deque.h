#pragma once

// containers/work_stealing_deque.h
//
// A work-stealing deque (#293, S1). The OWNER thread pushes and pops at the BACK
// (LIFO — newest task first: better locality, and it bounds how deep the live
// fork-join gets); THIEF threads steal from the FRONT (FIFO — the oldest task,
// usually the root of a large subtree, so a steal hands off a big chunk and
// rarely contends with the owner's end).
//
// This first cut is MUTEX-GUARDED: correct, with the right push/pop/steal
// discipline, but it serializes on the lock. The lock-free Chase-Lev upgrade (a
// growable atomic ring with an ABA-safe steal CAS) is a scalability follow-up and
// keeps this exact surface, so the pool above it does not change — tracked
// separately rather than risked in the first cut.
//
// Domain-agnostic, like the SPSC/MPSC queues beside it: the task pool instantiates
// it with its own Task type; the container knows nothing about tasks.

#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace wz::core::containers
{
    template <typename T>
    class WorkStealingDeque
    {
    public:
        WorkStealingDeque() = default;

        WorkStealingDeque(const WorkStealingDeque &) = delete;
        WorkStealingDeque &operator=(const WorkStealingDeque &) = delete;

        // Owner thread: push a task onto the back.
        void push(T value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            deque_.push_back(std::move(value));
        }

        // Owner thread: take the most-recently-pushed task (LIFO). nullopt if empty.
        std::optional<T> pop()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (deque_.empty())
                return std::nullopt;
            T value = std::move(deque_.back());
            deque_.pop_back();
            return value;
        }

        // Thief thread: take the oldest task (FIFO), from the front. nullopt if empty.
        std::optional<T> steal()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (deque_.empty())
                return std::nullopt;
            T value = std::move(deque_.front());
            deque_.pop_front();
            return value;
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return deque_.empty();
        }

    private:
        mutable std::mutex mutex_;
        std::deque<T> deque_;
    };
}
