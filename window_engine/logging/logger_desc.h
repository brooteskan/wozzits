#pragma once

#include <logging/logging.h>

namespace wz::logging
{
    namespace internal { struct MemoryLogSink; }

    struct LoggerDesc
    {
        LogLevel min_level          = LogLevel::Debug;
        bool     enable_stderr_sink = true;

        internal::MemoryLogSink* test_memory_sink = nullptr;
    };
}
