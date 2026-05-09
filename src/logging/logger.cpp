#include "logging/logger.h"
#include "logging/internal/logger_state.h"

namespace wz
{
    Logger::~Logger()
    {
        if (state)
        {
            state->stop();
            delete state;
            state = nullptr;
        }
    }

    void Logger::debug(std::string_view msg)    { logging::log(*this, LogLevel::Debug,    msg); }
    void Logger::info(std::string_view msg)     { logging::log(*this, LogLevel::Info,     msg); }
    void Logger::warn(std::string_view msg)     { logging::log(*this, LogLevel::Warning,  msg); }
    void Logger::error(std::string_view msg)    { logging::log(*this, LogLevel::Error,    msg); }
    void Logger::critical(std::string_view msg) { logging::log(*this, LogLevel::Critical, msg); }
}

namespace wz::logging
{
    bool init_logger(wz::Logger& logger, const LoggerDesc& desc)
    {
        if (logger.state)
            return false;

        auto* s = new internal::LoggerState();
        s->start(desc.min_level, desc.enable_stderr_sink, desc.test_memory_sink);
        logger.state = s;
        return true;
    }

    void shutdown_logger(wz::Logger& logger)
    {
        if (!logger.state)
            return;

        logger.state->stop();
        delete logger.state;
        logger.state = nullptr;
    }

    bool log(wz::Logger& logger, LogLevel level, std::string_view text)
    {
        if (!logger.state)
            return false;
        return logger.state->push(level, text);
    }

    void wait_until_idle(wz::Logger& logger)
    {
        if (logger.state)
            logger.state->wait_until_idle();
    }
}
