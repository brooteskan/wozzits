#pragma once

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
}

namespace wz::logging
{
    struct LogRecordView
    {
        wz::LogLevel level = wz::LogLevel::Info;

        const char* text = nullptr;
        std::size_t text_size = 0;

        uint64_t sequence = 0;
        uint64_t event_ticks = 0;
        uint64_t wall_time_ms = 0;

        const char* timestamp = nullptr;
        std::size_t timestamp_size = 0;
    };

    using LogSinkFn =
        void (*)(const LogRecordView& record, void* user);
}
