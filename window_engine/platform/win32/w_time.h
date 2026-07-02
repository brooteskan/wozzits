#pragma once

#include <windows.h>
#include <cstdint>

namespace wz::win32::time
{

    uint64_t ticks_per_second();

    uint64_t now_ticks();

    double now();
}