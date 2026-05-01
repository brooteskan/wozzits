#include <async/async.h>

namespace wz
{
    static IAsyncExecutor *g_executor = nullptr;

    void set_async_executor(IAsyncExecutor *executor)
    {
        g_executor = executor;
    }

    IAsyncExecutor *get_async_executor()
    {
        return g_executor;
    }
}