#include <windows.h>
#include <cstdint>

namespace wz::win32::time
{
    namespace
    { // lambda executes once when the module loads. No init_time() needed.
        double g_seconds_per_tick = []()
        {
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            return 1.0 / static_cast<double>(freq.QuadPart);
        }();

        uint64_t g_ticks_per_second = []()
        {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return static_cast<uint64_t>(f.QuadPart);
        }();
    }

    uint64_t ticks_per_second()
    {
        return g_ticks_per_second;
    }

    uint64_t now_ticks()
    {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return static_cast<uint64_t>(c.QuadPart);
    }

    double now()
    {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return counter.QuadPart * g_seconds_per_tick;
    }
}