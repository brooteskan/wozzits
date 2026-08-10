#include <async/async.h>

#include <atomic>

// The global async-executor seam. Atomic because the write and the reads are on
// DIFFERENT threads: install/clear happen on the engine thread at startup and
// teardown, while wz::fs::async_read_file / async_write_file read it from
// whatever thread calls them. A plain pointer here was a data race by the letter
// of the model, and the matching seams (task_scheduler.cpp's g_scheduler,
// diagnostic_sink.cpp's g_sink -- whose comment already claimed this file
// matched) were both already atomic.
namespace wz
{
    namespace
    {
        std::atomic<IAsyncExecutor*> g_executor{nullptr};
    }

    void set_async_executor(IAsyncExecutor *executor)
    {
        g_executor.store(executor, std::memory_order_release);
    }

    IAsyncExecutor *get_async_executor()
    {
        return g_executor.load(std::memory_order_acquire);
    }
}