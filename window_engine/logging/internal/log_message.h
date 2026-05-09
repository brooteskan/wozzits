#pragma once

#include <cstdint>
#include <cstddef>
#include <logging/logging.h>

namespace wz::logging::internal
{
    // Maximum number of characters stored per log message (excluding null terminator).
    // Messages longer than this are truncated on push.
    inline constexpr std::size_t kMaxLogMessageText = 255;

    struct LogMessage
    {
        uint64_t sequence    = 0;
        uint64_t event_ticks = 0;
        wz::LogLevel level   = wz::LogLevel::Info;
        char text[kMaxLogMessageText + 1] = {};
    };
}
