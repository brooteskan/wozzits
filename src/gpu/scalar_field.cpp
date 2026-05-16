// src/gpu/scalar_field.cpp

#include <gpu/scalar_field.h>

#include <gpu/dx12/dx12_internal.h>

namespace wz::gpu
{
    GPUHandle upload_scalar_field_texture(
        Device& device,
        const wz::engine::assets::ScalarFieldData& field)
    {
        if (!device.valid())
            return INVALID_GPU_HANDLE;

        return wz::gpu::dx12::internal::upload_scalar_field_texture_dx12(
            device,
            field);
    }
}