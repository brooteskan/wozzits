#include <gpu/gpu.h>
#include <gpu/dx12/dx12.h>
#include <platform/win32/win32.h>
namespace wz::gpu
{
    Device create_device(const wz::window::WindowHandle& window)
    {
#if defined(_WIN32)
        return wz::platform::win32::create_dx12_device(window);
#else
        static_assert(false, "Platform not implemented");
#endif
    }

    void destroy_device(Device& d)
    {
        dx12::destroy_device(d);
    }

    void resize(Device& d, int w, int h)
    {
        dx12::resize(d, w, h);
    }

    void begin_frame(Device& d)
    {
        dx12::begin_frame(d);
    }

    void clear(Device& d, float r, float g, float b, float a)
    {
        dx12::clear(d, r, g, b, a);
    }

    void end_frame(Device& d)
    {
        dx12::end_frame(d);
    }

    void present(Device& d)
    {
        dx12::present(d);
    }
}