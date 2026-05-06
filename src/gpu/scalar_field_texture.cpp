// file: src/gpu/scalar_field_texture.cpp

#include <gpu/scalar_field_texture.h>
#include <gpu/dx12/dx12_internal.h>
#include <engine/assets/scalar_field/scalar_field.h>

namespace wz::gpu
{
    GPUHandle upload_scalar_field_texture(
        Device& device,
        const wz::engine::assets::ScalarFieldData& field)
    {
        if (!device.valid())
            return INVALID_GPU_HANDLE;

        if (!field.valid())
            return INVALID_GPU_HANDLE;

        if (field.depth != 1)
            return INVALID_GPU_HANDLE;

        if (field.format != wz::engine::assets::ScalarFieldFormat::Float32)
            return INVALID_GPU_HANDLE;

        return wz::gpu::dx12::internal::upload_scalar_field_texture_dx12(
            device,
            field
        );
    }
}