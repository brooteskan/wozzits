#pragma once

#include <memory>
#include <functional>
#include <string_view>
#include <logging/logging.h>

namespace wz::core
{
    class LoggerWorker;
}

namespace wz
{
    enum class LogFileSinkMode
    {
        Immediate,   // write per log (slow but safe)
        Buffered,    // accumulate + periodic flush
        ShutdownOnly // current behavior (debug only)
    };

    // Public-facing logging system.
    class Logger
    {
    public:
        using Callback = std::function<void(LogLevel, std::string_view)>;

        Logger();
        ~Logger();

        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;

        // Logging API (thread-safe, lock-free push)
        void log(LogLevel level, std::string_view message);

        // Optional override for output sink
        void set_callback(LogSinkType cb);

        void flush();

        // Convenience helpers
        void debug(std::string_view msg);
        void info(std::string_view msg);
        void warn(std::string_view msg);
        void error(std::string_view msg);
        void critical(std::string_view msg);

#ifdef WZ_ENABLE_TESTING
        std::vector<LogEvent> snapshot_memory() const;
        void wait_until_idle();

#endif
    private:
        std::unique_ptr<core::LoggerWorker> impl;
    };
}