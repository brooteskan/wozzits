#pragma once

// gpu/vector_field.h
//
// Public GPU upload API for CPU-side VectorFieldData.

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <engine/assets/vector_field/vector_field.h>

namespace wz::gpu
{
    [[nodiscard]] GPUHandle upload_vector_field_texture(
        Device& device,
        const wz::engine::assets::VectorFieldData& field);
}
