#include "logging/internal/logger_state.h"
#include "logging/internal/memory_log_sink.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <thread>

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

        void format_timestamp(
            uint64_t wall_time_ms,
            char* out,
            std::size_t out_size)
        {
            if (!out || out_size == 0u) {
                return;
            }

            const std::time_t seconds =
                static_cast<std::time_t>(wall_time_ms / 1000u);
            const unsigned millis =
                static_cast<unsigned>(wall_time_ms % 1000u);

            std::tm local_time{};
#ifdef _WIN32
            localtime_s(&local_time, &seconds);
#else
            localtime_r(&seconds, &local_time);
#endif
            std::snprintf(
                out,
                out_size,
                "%02d:%02d:%02d.%03u",
                local_time.tm_hour,
                local_time.tm_min,
                local_time.tm_sec,
                millis);
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
    } // anonymous namespace

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

        // BACKOFF, not a bare yield (#313, B4-C10). std::this_thread::yield()
        // maps to SwitchToThread() on Windows, which yields only to a ready
        // thread on the SAME processor and returns immediately when there is
        // none -- so an idle queue made this a pure spin. Measured: 99.0% of one
        // core over a 3s idle window with NOTHING being logged, for the lifetime
        // of every engine process.
        //
        // Spin briefly first so a burst still drains at full speed, then sleep.
        // The cost is up to kIdleSleep of extra latency on the first message
        // after an idle period, which for logging is not a cost at all.
        //
        // Deliberately NOT a condition variable: that would put a mutex in
        // LoggerState::push, which is reachable from ANY thread -- including the
        // audio realtime callback, where taking a lock is a priority-inversion
        // hazard. The producer side stays lock-free.
        constexpr int kSpinsBeforeSleep = 64;
        constexpr auto kIdleSleep = std::chrono::milliseconds(1);
        int idle_spins = 0;

        while (running_.load(std::memory_order_acquire))
        {
            if (queue_.try_pop(msg))
            {
                idle_spins = 0;

                dispatch(msg);

                if (in_flight_.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    std::lock_guard lock(idle_mutex_);
                    idle_cv_.notify_all();
                }
            }
            else if (idle_spins < kSpinsBeforeSleep)
            {
                ++idle_spins;
                std::this_thread::yield();
            }
            else
            {
                std::this_thread::sleep_for(kIdleSleep);
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
        char timestamp[16]{};
        format_timestamp(msg.wall_time_ms, timestamp, sizeof(timestamp));

        if (stderr_sink_)
            std::fprintf(stderr, "[%s] [%s] %s\n", timestamp, lvl, msg.text);

#ifdef _WIN32
        if (debugger_sink_)
        {
            char buf[kMaxLogMessageText + 32];
            std::snprintf(
                buf,
                sizeof(buf),
                "[%s] [%s] %s\n",
                timestamp,
                lvl,
                msg.text);
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

            char buf[kMaxLogMessageText + 32];
            int len = std::snprintf(
                buf,
                sizeof(buf),
                "[%s] [%s] %s\n",
                timestamp,
                lvl,
                msg.text);
            DWORD written = 0;
            WriteConsoleA(h, buf, static_cast<DWORD>(len), &written, nullptr);

            SetConsoleTextAttribute(h, saved);
        }
#endif

        if (memory_sink_)
            memory_sink_->write(msg);

        wz::logging::LogSinkFn sink = nullptr;
        void* user = nullptr;

        {
            std::lock_guard lock(tool_sink_mutex_);
            sink = tool_sink_;
            user = tool_sink_user_;
        }

        if (sink)
        {
            const std::size_t len =
                std::strlen(msg.text);

            wz::logging::LogRecordView record{
                .level = msg.level,
                .text = msg.text,
                .text_size = len,
                .sequence = msg.sequence,
                .event_ticks = msg.event_ticks,
                .wall_time_ms = msg.wall_time_ms,
                .timestamp = timestamp,
                .timestamp_size = std::strlen(timestamp),
            };

            sink(record, user);
        }

    }

    void LoggerState::set_sink(
        wz::logging::LogSinkFn sink,
        void* user)
    {
        std::lock_guard lock(tool_sink_mutex_);

        tool_sink_ = sink;
        tool_sink_user_ = user;
    }
}
