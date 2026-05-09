#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

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

        bool try_push(wz::LogLevel level, std::string text);
        bool try_pop(LogMessage& out);

        void close();

        bool is_accepting() const;
        bool is_closed()    const;

        uint64_t dropped_count()   const;
        uint64_t submitted_count() const;

    private:
        uint64_t next_sequence();
        uint64_t now_ticks() const;

    private:
        std::atomic<bool>     accepting_     { true };
        std::atomic<uint64_t> next_sequence_ { 1 };
        std::atomic<uint64_t> dropped_count_ { 0 };
        std::atomic<uint64_t> submitted_count_{ 0 };

        // Heap-allocated: ~3.5 MB at kLoggerQueueCapacity=65536, too large for stack
        std::unique_ptr<wz::core::containers::MPSCRingBuffer<LogMessage, kLoggerQueueCapacity>> queue_;
    };
}
