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

        bool enable_debugger_sink = false; // OutputDebugStringA (Windows only)
        bool enable_console_sink  = false; // Win32 console window with level colors
    };
}
