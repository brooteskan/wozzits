#pragma once

#include <string>
#include <cstdint>
#include <logging/logging.h>

namespace wz::logging::internal
{
    struct LogMessage
    {
        uint64_t sequence    = 0;
        uint64_t event_ticks = 0;
        wz::LogLevel level   = wz::LogLevel::Info;
        std::string text{};
    };
}
