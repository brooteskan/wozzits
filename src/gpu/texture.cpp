// src/gpu/texture.cpp

#include <gpu/texture.h>
#include <gpu/dx12/dx12_internal.h>

namespace wz::gpu
{
    GPUHandle create_texture(Device& device, const TextureDesc& desc)
    {
        if (!device_ok(device)) {
            return {};
        }
        return dx12::internal::create_texture_dx12(device, desc);
    }

    bool update_texture(
        Device& device,
        GPUHandle handle,
        const void* data,
        uint64_t size,
        uint64_t offset)
    {
        if (!device_ok(device)) {
            return false;
        }
        return dx12::internal::update_texture_dx12(
            device, handle, data, size, offset);
    }

    bool update_texture_mip(
        Device& device,
        GPUHandle handle,
        uint32_t mip_level,
        const void* data,
        uint64_t size)
    {
        if (!device_ok(device)) {
            return false;
        }
        return dx12::internal::update_texture_mip_dx12(
            device, handle, mip_level, data, size);
    }

    bool release_texture(Device& device, GPUHandle handle)
    {
        if (!device_ok(device)) {
            return false;
        }
        return dx12::internal::release_texture_dx12(device, handle);
    }
}
