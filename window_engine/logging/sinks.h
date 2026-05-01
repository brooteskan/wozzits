#pragma once
#include "logging.h"
#include <mutex>
#include <vector>

namespace wz::core
{

    class BufferSink
    {
    public:
        void write(const LogEvent &e)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            buffer_.push_back(e);
        }

        std::vector<LogEvent> snapshot() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return buffer_;
        }

        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            buffer_.clear();
        }

    private:
        mutable std::mutex mutex_;
        std::vector<LogEvent> buffer_;
    };

}