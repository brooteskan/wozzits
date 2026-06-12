#include <gpu/gpu.h>
#include <gpu/dx12/dx12.h>
#include <gpu/dx12/dx12_internal.h>
#include <platform/win32/win32.h>
#include <render/frame/render_frame.h>

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

    //void submit_frame(Device& d, wz::render::RenderFrame& f)
    //{
    //    auto* impl = (wz::gpu::dx12::DX12Device*)d.impl;
    //    auto* ctx = impl->ctx;
    //}

    void destroy_device(Device& d)
    {
        wz::gpu::dx12::destroy_device(d);
    }

    DeviceStatus device_status(const Device& d)
    {
        return wz::gpu::dx12::device_status(d);
    }

    const DeviceLostInfo* device_lost_info(const Device& d)
    {
        return wz::gpu::dx12::device_lost_info(d);
    }

    bool device_ok(const Device& d)
    {
        return d.valid() && device_status(d) == DeviceStatus::Ok;
    }

    bool resize(Device& d, int w, int h)
    {
        return wz::gpu::dx12::resize(d, w, h);
    }

    void wait_idle(Device& d)
    {
        wz::gpu::dx12::wait_idle(d);
    }

    bool begin_frame(Device& d)
    {
        return wz::gpu::dx12::begin_frame(d);
    }

    void clear(Device& d, float r, float g, float b, float a)
    {
        wz::gpu::dx12::clear(d, r, g, b, a);
    }

    bool end_frame(Device& d)
    {
        return wz::gpu::dx12::end_frame(d);
    }

    bool present(Device& d)
    {
        return wz::gpu::dx12::present(d);
    }

    bool present(Device& d, uint32_t sync_interval)
    {
        return wz::gpu::dx12::present(d, sync_interval);
    }
}
