#include "logging/logger_worker.h"
#include "logging/sinks.h"

#include <mutex>
#include <cstdio>
#include <file/filesystem.h>

namespace wz::core
{
    LoggerWorker::LoggerWorker()
    {
        callback = [](LogLevel level, const char *msg)
        {
            const char *level_str = "";

            switch (level)
            {
            case LogLevel::Debug:
                level_str = "DEBUG";
                break;
            case LogLevel::Info:
                level_str = "INFO";
                break;
            case LogLevel::Warning:
                level_str = "WARN";
                break;
            case LogLevel::Error:
                level_str = "ERROR";
                break;
            case LogLevel::Critical:
                level_str = "CRITICAL";
                break;
            }

            std::fprintf(stderr, "[%s] %s\n", level_str, msg);
        };
    }

    LoggerWorker::~LoggerWorker()
    {
        stop();
    }

    void LoggerWorker::start()
    {
        running.store(true, std::memory_order_release);
        worker = std::thread(&LoggerWorker::run, this);
    }

    void LoggerWorker::stop()
    {
        accepting.store(false, std::memory_order_release);
        running.store(false, std::memory_order_release);

        if (worker.joinable())
            worker.join();

        // now everything is guaranteed drained
        flush_file();
    }

    void LoggerWorker::set_callback(LogSinkType type)
    {
        switch (type)
        {
        case LogSinkType::Stderr:
            callback = [](LogLevel level, const char *msg)
            {
                std::fprintf(stderr, "[%d] %s\n", (int)level, msg);
            };
            break;

        case LogSinkType::Debugger:
            callback = [](LogLevel level, const char *msg)
            {
                // platform specific hook
                // OutputDebugStringA(msg);
            };
            break;

        case LogSinkType::File:
        {
            callback = [this](LogLevel level, const char *msg)
            {
                std::lock_guard lock(file_mutex);
                file_buffer.emplace_back(msg);
            };
            break;
        }

        case LogSinkType::Buffer:
        {
            buffer_.clear();

            callback = [this](LogLevel level, const char *msg)
            {
                std::lock_guard lock(buffer_mutex_);
                buffer_.push_back(LogEvent{level, msg});
            };
            break;
        }

        case LogSinkType::Null:
            callback = [](LogLevel, const char *) {};
            break;
        }
    }

    void LoggerWorker::push(LogEvent event)
    {
        if (!accepting.load())
            return;
        in_flight.fetch_add(1, std::memory_order_acq_rel);
        queue.push(std::move(event));
    }

    void LoggerWorker::run()
    {
        LogEvent e;

        while (running.load(std::memory_order_acquire))
        {
            if (queue.try_pop(e))
            {
                callback(e.level, e.message.c_str());

                if (in_flight.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    std::lock_guard lock(idle_mutex);
                    idle_cv.notify_all();
                }
            }
            else
            {
                std::this_thread::yield();
            }
        }

        // final drain safety
        while (queue.try_pop(e))
        {
            callback(e.level, e.message.c_str());
            in_flight.fetch_sub(1, std::memory_order_acq_rel);
        }

        std::lock_guard lock(idle_mutex);
        idle_cv.notify_all();
    }

    void LoggerWorker::flush_file()
    {
        std::lock_guard lock(file_mutex);

        if (file_buffer.empty())
            return;

        std::string combined;
        combined.reserve(file_buffer.size() * 64);

        for (auto &line : file_buffer)
        {
            combined += line;
            combined += "\n";
        }

        wz::fs::write_file_text("wozzits.log", combined, true);
    }

    void LoggerWorker::wait_until_idle()
    {
        std::unique_lock lock(idle_mutex);

        idle_cv.wait(lock, [&]
                     { return in_flight.load(std::memory_order_acquire) == 0; });
    }

#ifdef WZ_ENABLE_TESTING
    std::vector<LogEvent> LoggerWorker::snapshot_memory() const
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        return buffer_;
    }

#endif

}