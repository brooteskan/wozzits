#include "logging/internal/logger_state.h"
#include "logging/internal/memory_log_sink.h"

#include <cstdio>

namespace wz::logging::internal
{
    void LoggerState::start(LogLevel min_level, bool stderr_sink, MemoryLogSink* memory_sink)
    {
        min_level_   = min_level;
        stderr_sink_ = stderr_sink;
        memory_sink_ = memory_sink;

        running_.store(true, std::memory_order_release);
        worker_ = std::thread(&LoggerState::run, this);
    }

    void LoggerState::stop()
    {
        queue_.close();
        running_.store(false, std::memory_order_release);

        if (worker_.joinable())
            worker_.join();
    }

    bool LoggerState::push(LogLevel level, std::string_view text)
    {
        if (static_cast<int>(level) < static_cast<int>(min_level_))
            return false;

        in_flight_.fetch_add(1, std::memory_order_acq_rel);

        if (!queue_.try_push(level, text))
        {
            if (in_flight_.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::lock_guard lock(idle_mutex_);
                idle_cv_.notify_all();
            }
            return false;
        }

        return true;
    }

    void LoggerState::wait_until_idle()
    {
        std::unique_lock lock(idle_mutex_);
        idle_cv_.wait(lock, [&]
        {
            return in_flight_.load(std::memory_order_acquire) == 0;
        });
    }

    void LoggerState::run()
    {
        LogMessage msg;

        while (running_.load(std::memory_order_acquire))
        {
            if (queue_.try_pop(msg))
            {
                dispatch(msg);

                if (in_flight_.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    std::lock_guard lock(idle_mutex_);
                    idle_cv_.notify_all();
                }
            }
            else
            {
                std::this_thread::yield();
            }
        }

        // Final drain after running_ goes false
        while (queue_.try_pop(msg))
        {
            dispatch(msg);
            in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        }

        std::lock_guard lock(idle_mutex_);
        idle_cv_.notify_all();
    }

    void LoggerState::dispatch(const LogMessage& msg)
    {
        if (stderr_sink_)
        {
            const char* level_str = "";
            switch (msg.level)
            {
            case LogLevel::Debug:    level_str = "DEBUG";    break;
            case LogLevel::Info:     level_str = "INFO";     break;
            case LogLevel::Warning:  level_str = "WARN";     break;
            case LogLevel::Error:    level_str = "ERROR";    break;
            case LogLevel::Critical: level_str = "CRITICAL"; break;
            }
            std::fprintf(stderr, "[%s] %s\n", level_str, msg.text);
        }

        if (memory_sink_)
            memory_sink_->write(msg);
    }
}
