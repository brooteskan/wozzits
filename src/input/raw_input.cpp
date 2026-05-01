#include <platform/win32/ri_win32.h>


namespace wz::input
{
    void init_raw_input()
    {
        wz::platform::win32::ri_init(GetModuleHandle(nullptr));
    }

    void shutdown_raw_input()
    {
        wz::platform::win32::ri_shutdown();
    }
}
