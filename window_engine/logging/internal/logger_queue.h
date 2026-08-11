#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>

#include <containers/mpsc_ring_buffer.h>
#include "logging/internal/log_message.h"

namespace wz::logging::internal
{
    inline constexpr std::size_t kLoggerQueueCapacity = 65536;

    class LoggerQueue
    {
    public:
        LoggerQueue();

        LoggerQueue(const LoggerQueue&)            = delete;
        LoggerQueue& operator=(const LoggerQueue&) = delete;
        LoggerQueue(LoggerQueue&&)                 = delete;
        LoggerQueue& operator=(LoggerQueue&&)      = delete;

        bool try_push(wz::LogLevel level, std::string_view text);
        bool try_pop(LogMessage& out);

        void close();

        bool is_accepting() const;
        bool is_closed()    const;

        // True when nothing is claimed in the ring. Distinct from "try_pop
        // returned false", which is also true at a slot whose producer has
        // claimed it but not yet published -- see MPSCRingBuffer::try_pop. The
        // shutdown drain needs the difference to avoid abandoning messages
        // queued behind such a hole.
        bool empty() const;

        uint64_t dropped_count()   const;
        uint64_t submitted_count() const;

    private:
        uint64_t next_sequence();
        uint64_t now_ticks() const;
        uint64_t now_wall_time_ms() const;

    private:
        std::atomic<bool>     accepting_     { true };
        std::atomic<uint64_t> next_sequence_ { 1 };
        std::atomic<uint64_t> dropped_count_ { 0 };
        std::atomic<uint64_t> submitted_count_{ 0 };

        // Heap-allocated: ~19 MB at kLoggerQueueCapacity=65536 (a LogMessage is
        // ~296 B -- it carries its text inline so a push never allocates), far
        // too large for the stack.
        std::unique_ptr<wz::core::containers::MPSCRingBuffer<LogMessage, kLoggerQueueCapacity>> queue_;
    };
}
