#pragma once
// gpu/vector_field_texture.h

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

namespace wz::engine::assets {
    struct VectorFieldData;
}

namespace wz::gpu {

    [[nodiscard]] GPUHandle upload_vector_field_texture(
        Device& device,
        const wz::engine::assets::VectorFieldData& field
    );

}
