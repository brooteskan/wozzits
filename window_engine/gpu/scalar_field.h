#pragma once

// gpu/scalar_field.h
//
// Public GPU upload API for CPU-side ScalarFieldData.
//
// This is the backend-neutral entry point used by runtime/rendering code.
// Backend-specific upload implementations live under gpu/dx12/internal.

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <engine/assets/scalar_field/scalar_field.h>

namespace wz::gpu
{
    [[nodiscard]] GPUHandle upload_scalar_field_texture(
        Device& device,
        const wz::engine::assets::ScalarFieldData& field);
}