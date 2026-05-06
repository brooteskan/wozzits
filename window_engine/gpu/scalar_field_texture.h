#pragma once
// gpu/scalar_field_texture.h

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

namespace wz::engine::assets {
    struct ScalarFieldData;
}

namespace wz::gpu {

    [[nodiscard]] GPUHandle upload_scalar_field_texture(
        Device& device,
        const wz::engine::assets::ScalarFieldData& field
    );

}