#include "logging/internal/logger_state.h"
#include "logging/internal/memory_log_sink.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace wz::logging::internal
{
    namespace
    {
        const char* level_str(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Debug:    return "DEBUG";
            case LogLevel::Info:     return "INFO";
            case LogLevel::Warning:  return "WARN";
            case LogLevel::Error:    return "ERROR";
            case LogLevel::Critical: return "CRITICAL";
            }
            return "";
        }

#ifdef _WIN32
        WORD console_color(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Debug:    return FOREGROUND_INTENSITY;                               // dark gray
            case LogLevel::Info:     return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // white
            case LogLevel::Warning:  return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; // yellow
            case LogLevel::Error:    return FOREGROUND_RED | FOREGROUND_INTENSITY;               // bright red
            case LogLevel::Critical: return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // magenta
            }
            return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        }
#endif
    }

    void LoggerState::start(const wz::logging::LoggerDesc& desc)
    {
        min_level_      = desc.min_level;
        stderr_sink_    = desc.enable_stderr_sink;
        debugger_sink_  = desc.enable_debugger_sink;
        console_sink_   = desc.enable_console_sink;
        memory_sink_    = desc.test_memory_sink;

#ifdef _WIN32
        if (console_sink_)
        {
            if (!AttachConsole(ATTACH_PARENT_PROCESS))
                AllocConsole();
            console_handle_ = GetStdHandle(STD_OUTPUT_HANDLE);
        }
#endif

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
        const char* lvl = level_str(msg.level);

        if (stderr_sink_)
            std::fprintf(stderr, "[%s] %s\n", lvl, msg.text);

#ifdef _WIN32
        if (debugger_sink_)
        {
            char buf[kMaxLogMessageText + 16];
            std::snprintf(buf, sizeof(buf), "[%s] %s\n", lvl, msg.text);
            OutputDebugStringA(buf);
        }

        if (console_sink_ && console_handle_)
        {
            HANDLE h = static_cast<HANDLE>(console_handle_);

            // Save current attributes so we can restore them after writing
            CONSOLE_SCREEN_BUFFER_INFO info{};
            GetConsoleScreenBufferInfo(h, &info);
            WORD saved = info.wAttributes;

            SetConsoleTextAttribute(h, console_color(msg.level));

            char buf[kMaxLogMessageText + 16];
            int  len = std::snprintf(buf, sizeof(buf), "[%s] %s\n", lvl, msg.text);
            DWORD written = 0;
            WriteConsoleA(h, buf, static_cast<DWORD>(len), &written, nullptr);

            SetConsoleTextAttribute(h, saved);
        }
#endif

        if (memory_sink_)
            memory_sink_->write(msg);
    }
}
