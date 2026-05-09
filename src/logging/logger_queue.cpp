#include "logging/internal/logger_queue.h"

#include <chrono>

namespace wz::logging::internal
{
    LoggerQueue::LoggerQueue()
        : queue_(std::make_unique<wz::core::containers::MPSCRingBuffer<LogMessage, kLoggerQueueCapacity>>())
    {}

    bool LoggerQueue::try_push(wz::LogLevel level, std::string text)
    {
        if (!accepting_.load(std::memory_order_acquire))
        {
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        LogMessage msg{};
        msg.sequence    = next_sequence();
        msg.event_ticks = now_ticks();
        msg.level       = level;
        msg.text        = std::move(text);

        if (!queue_->try_push(std::move(msg)))
        {
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        submitted_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool LoggerQueue::try_pop(LogMessage& out)
    {
        return queue_->try_pop(out);
    }

    void LoggerQueue::close()
    {
        accepting_.store(false, std::memory_order_release);
    }

    bool LoggerQueue::is_accepting() const
    {
        return accepting_.load(std::memory_order_acquire);
    }

    bool LoggerQueue::is_closed() const
    {
        return !accepting_.load(std::memory_order_acquire);
    }

    uint64_t LoggerQueue::dropped_count() const
    {
        return dropped_count_.load(std::memory_order_relaxed);
    }

    uint64_t LoggerQueue::submitted_count() const
    {
        return submitted_count_.load(std::memory_order_relaxed);
    }

    uint64_t LoggerQueue::next_sequence()
    {
        return next_sequence_.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t LoggerQueue::now_ticks() const
    {
        using clock = std::chrono::steady_clock;
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now().time_since_epoch()
            ).count()
        );
    }
}
