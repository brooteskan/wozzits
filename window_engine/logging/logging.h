#pragma once
#include <string>
namespace wz
{
    enum class LogLevel
    {
        Debug = 0,
        Info,
        Warning,
        Error,
        Critical
    };

    enum class LogSinkType
    {
        Stderr = 0,
        File,
        Debugger,
        Buffer,
        Null
    };

    struct LogEvent
    {
        LogLevel level;
        std::string message;
    };
}