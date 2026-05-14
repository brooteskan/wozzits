#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <logging/logging.h>
#include <logging/logger_desc.h>
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

        void start(const wz::logging::LoggerDesc& desc);
        void stop();

        bool push(LogLevel level, std::string_view text);

        void wait_until_idle();

        void set_sink(
            wz::logging::LogSinkFn sink,
            void* user);

    private:
        void run();
        void dispatch(const LogMessage& msg);

        LogLevel         min_level_      = LogLevel::Debug;
        bool             stderr_sink_   = false;
        bool             debugger_sink_ = false;
        bool             console_sink_  = false;
        void*            console_handle_ = nullptr; // HANDLE, avoids <windows.h> in header
        MemoryLogSink*   memory_sink_   = nullptr;

        wz::logging::LogSinkFn tool_sink_ = nullptr;
        void* tool_sink_user_ = nullptr;
        std::mutex tool_sink_mutex_;

        LoggerQueue               queue_;
        std::thread               worker_;
        std::atomic<bool>         running_   { false };
        std::atomic<int>          in_flight_ { 0 };
        std::mutex                idle_mutex_;
        std::condition_variable   idle_cv_;
    };
}
