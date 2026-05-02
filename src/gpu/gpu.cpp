#include <gpu/gpu.h>
#include <gpu/dx12/dx12.h>
#include <gpu/dx12/dx12_internal.h>
#include <platform/win32/win32.h>
#include <engine/render_backends/dx12/dx12_submit.h>

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
        wz::gpu::dx12::destroy_device(d);
    }

    void resize(Device& d, int w, int h)
    {
        wz::gpu::dx12::resize(d, w, h);
    }

    void begin_frame(Device& d)
    {
        wz::gpu::dx12::begin_frame(d);
    }

    void clear(Device& d, float r, float g, float b, float a)
    {
        wz::gpu::dx12::clear(d, r, g, b, a);
    }

    void end_frame(Device& d)
    {
        wz::gpu::dx12::end_frame(d);
    }

    void present(Device& d)
    {
        wz::gpu::dx12::present(d);
    }
}