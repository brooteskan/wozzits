#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <logging/logging.h>
#include "logging/internal/logger_queue.h"

namespace wz::logging::internal
{
    struct MemoryLogSink;

    struct LoggerState
    {
        LoggerState()  = default;
        ~LoggerState() = default;

        LoggerState(const LoggerState&)            = delete;
        LoggerState& operator=(const LoggerState&) = delete;
        LoggerState(LoggerState&&)                 = delete;
        LoggerState& operator=(LoggerState&&)      = delete;

        void start(LogLevel min_level, bool stderr_sink, MemoryLogSink* memory_sink);
        void stop();

        bool push(LogLevel level, std::string_view text);

        void wait_until_idle();

    private:
        void run();
        void dispatch(const LogMessage& msg);

        LogLevel         min_level_   = LogLevel::Debug;
        bool             stderr_sink_ = false;
        MemoryLogSink*   memory_sink_ = nullptr;

        LoggerQueue               queue_;
        std::thread               worker_;
        std::atomic<bool>         running_   { false };
        std::atomic<int>          in_flight_ { 0 };
        std::mutex                idle_mutex_;
        std::condition_variable   idle_cv_;
    };
}
