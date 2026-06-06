#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>

class ThreadTestHarness
{
public:
    ThreadTestHarness() = default;

    ~ThreadTestHarness()
    {
        join_all();
    }

    // Spawn N worker threads
    template <typename Fn>
    void spawn(size_t count, Fn &&fn)
    {
        for (size_t i = 0; i < count; ++i)
        {
            threads.emplace_back([this, fn = std::forward<Fn>(fn), i]()
                                 {
            wait_for_start();
            fn(i); });
        }
    }

    // Release all threads simultaneously
    void start()
    {
        {
            std::lock_guard<std::mutex> lock(start_mutex);
            started = true;
        }

        start_cv.notify_all();
    }

    // Wait for all threads to complete
    void join_all()
    {
        for (auto &t : threads)
        {
            if (t.joinable())
                t.join();
        }

        threads.clear();
    }

private:
    void wait_for_start()
    {
        std::unique_lock<std::mutex> lock(start_mutex);
        start_cv.wait(lock, [&]
                      { return started; });
    }

private:
    std::vector<std::thread> threads;

    std::mutex start_mutex;
    std::condition_variable start_cv;
    bool started{false};
};
