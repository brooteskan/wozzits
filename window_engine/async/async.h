#pragma once

#include <functional>

namespace wz
{
    /**
     * @brief Interface for submitting asynchronous work.
     *
     * An async executor is responsible for scheduling jobs for execution on
     * worker threads or other asynchronous systems.
     *
     * The engine does not mandate how execution is implemented; implementations
     * may use thread pools, task systems, or platform schedulers.
     *
     * Thread Safety:
     *     Implementations must be safe to call from multiple threads unless
     *     explicitly documented otherwise.
     */
    struct IAsyncExecutor
    {
        virtual ~IAsyncExecutor() = default;

        /**
         * @brief Submit a job for asynchronous execution.
         *
         * The provided callable will be executed by the executor at some point
         * in the future. Execution may occur on a worker thread or other
         * asynchronous context depending on the implementation.
         *
         * @param job Callable object representing the work to perform.
         *
         * @note This call must return quickly and must not block.
         */
        virtual void post(std::function<void()> job) = 0;
    };

    /**
     * @brief Install the global async executor used by the engine.
     *
     * This function registers the executor that will handle asynchronous work
     * submitted through engine systems.
     *
     * @param executor Pointer to the executor instance.
     *
     * @note The caller retains ownership of the executor.
     * @warning This function should typically be called only once during engine initialization.
     * @warning The executor must remain valid for as long as it is registered.
     */
    void set_async_executor(IAsyncExecutor *executor);

    /**
     * @brief Retrieve the currently registered async executor.
     *
     * @return Pointer to the active executor, or nullptr if none has been set.
     */
    IAsyncExecutor *get_async_executor();
}