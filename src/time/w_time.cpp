#include <time/w_time.h>
#include <platform/win32/w_time.h> // or declare functions directly

namespace wz::time
{

    double TimeSource::now()
    {
        return wz::win32::time::now();
    }

    Tick TimeSource::now_ticks()
    {
        return wz::win32::time::now_ticks();
    }

    uint64_t TimeSource::ticks_per_second()
    {
        return wz::win32::time::ticks_per_second();
    }
}