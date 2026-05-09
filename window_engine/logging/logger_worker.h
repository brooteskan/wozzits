#pragma once

#include <atomic>
#include <thread>
#include <functional>
#include <mutex>
#include <vector>
#include <string>

#include "logging/internal/logger_queue.h"
#include "logging.h"

namespace wz::core
{

    class LoggerWorker
    {
    public:
        using Callback = std::function<void(LogLevel, const char *)>;

        LoggerWorker();
        ~LoggerWorker();

        LoggerWorker(const LoggerWorker &) = delete;
        LoggerWorker &operator=(const LoggerWorker &) = delete;

        void start();
        void stop();

        void push(LogEvent event);

        void set_callback(LogSinkType t);

        void flush();

        void flush_file();

        void wait_until_idle();

#ifdef WZ_ENABLE_TESTING
        std::vector<LogEvent> snapshot_memory() const;

#endif

    private:
        void run();

    private:
        wz::logging::internal::LoggerQueue queue_;

        std::vector<LogEvent> buffer_;
        mutable std::mutex buffer_mutex_;

        std::thread worker;
        std::atomic<bool> running{ false };

        Callback callback;

        std::atomic<int> in_flight{ 0 };
        std::mutex idle_mutex;
        std::condition_variable idle_cv;

        std::vector<std::string> file_buffer;
        std::mutex file_mutex;
    };
}
