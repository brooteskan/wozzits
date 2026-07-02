#pragma once

#include <windows.h>

namespace wz::platform::win32
{
    bool ri_init(HINSTANCE hInstance);
    void ri_shutdown();

    void ri_process_input(LPARAM lParam);
}