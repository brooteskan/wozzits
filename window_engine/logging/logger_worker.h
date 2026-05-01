#pragma once

#include <atomic>
#include <thread>
#include <functional>
#include <mutex>
#include <vector>
#include <string>

#include <containers/mpsc_queue.h>
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

        void flush()
        {
            LogEvent event;

            while (queue.try_pop(event))
            {
                callback(event.level, event.message.c_str());
            }
        }

        void flush_file();

        void wait_until_idle();

#ifdef WZ_ENABLE_TESTING
        std::vector<LogEvent> snapshot_memory() const;

#endif

    private:
        void run();

    private:
        containers::MPSCQueue<LogEvent> queue;

        std::vector<LogEvent> buffer_; // for LogSinkType::Buffer

        mutable std::mutex buffer_mutex_;

        std::thread worker;
        std::atomic<bool> running{false};

        Callback callback;

        std::atomic<int> in_flight{0};
        std::mutex idle_mutex;
        std::condition_variable idle_cv;

        std::atomic<bool> accepting = true;

        std::vector<std::string> file_buffer;
        std::mutex file_mutex;
    };
}