#pragma once

#include <string_view>

#include <logging/logging.h>
#include <logging/logger_desc.h>

namespace wz::logging::internal { struct LoggerState; }

namespace wz
{
    struct Logger
    {
        logging::internal::LoggerState* state = nullptr;

        ~Logger();
        Logger() = default;

        Logger(const Logger&)            = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&)                 = delete;
        Logger& operator=(Logger&&)      = delete;

        // Convenience wrappers — delegate to wz::logging::log
        void debug(std::string_view msg);
        void info(std::string_view msg);
        void warn(std::string_view msg);
        void error(std::string_view msg);
        void critical(std::string_view msg);
    };
}

namespace wz::logging
{
    bool init_logger(wz::Logger& logger, const LoggerDesc& desc);
    void shutdown_logger(wz::Logger& logger);

    bool log(wz::Logger& logger, LogLevel level, std::string_view text);
    void wait_until_idle(wz::Logger& logger);
}
