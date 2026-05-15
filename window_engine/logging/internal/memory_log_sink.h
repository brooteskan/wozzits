#pragma once
// logging/internal/memory_log_sink.h

#include <mutex>
#include <vector>
#include "logging/internal/log_message.h"

namespace wz::logging::internal
{
    struct MemoryLogSink
    {
        void write(const LogMessage& msg)
        {
            std::lock_guard lock(mutex_);
            messages_.push_back(msg);
        }

        std::vector<LogMessage> snapshot() const
        {
            std::lock_guard lock(mutex_);
            return messages_;
        }

        std::size_t size() const
        {
            std::lock_guard lock(mutex_);
            return messages_.size();
        }

        void clear()
        {
            std::lock_guard lock(mutex_);
            messages_.clear();
        }

    private:
        mutable std::mutex      mutex_;
        std::vector<LogMessage> messages_;
    };
}
