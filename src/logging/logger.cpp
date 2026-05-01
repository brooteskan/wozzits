#include <logging/logger.h>
#include <logging/logger_worker.h>

namespace wz
{
    Logger::Logger()
        : impl(std::make_unique<core::LoggerWorker>())
    {
        impl->start();
    }

    Logger::~Logger()
    {
    }

    void Logger::log(LogLevel level, std::string_view message)
    {
        impl->push(
            wz::LogEvent{
                static_cast<wz::LogLevel>(level),
                message.data()});
    }

    void Logger::set_callback(LogSinkType type)
    {
        impl->set_callback(type);
    }

    void Logger::flush()
    {
        impl->flush();
    }

    void Logger::debug(std::string_view msg) { log(LogLevel::Debug, msg); }
    void Logger::info(std::string_view msg) { log(LogLevel::Info, msg); }
    void Logger::warn(std::string_view msg) { log(LogLevel::Warning, msg); }
    void Logger::error(std::string_view msg) { log(LogLevel::Error, msg); }
    void Logger::critical(std::string_view msg) { log(LogLevel::Critical, msg); }

#ifdef WZ_ENABLE_TESTING
    std::vector<LogEvent> Logger::snapshot_memory() const
    {
        return impl->snapshot_memory(); // legal: inside .cpp, incomplete type is OK
    }

    void Logger::wait_until_idle()
    {
        impl->wait_until_idle();
    }
#endif
}