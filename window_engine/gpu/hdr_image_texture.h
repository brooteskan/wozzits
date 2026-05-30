#pragma once

// gpu/hdr_image_texture.h
//
// Minimal float image upload boundary for sky/equirectangular HDR visuals.
// This is intentionally narrower than a future general TextureAsset pipeline.

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

namespace wz::engine::assets {
    struct HDRImageData;
}

namespace wz::gpu
{
    [[nodiscard]] GPUHandle upload_hdr_image_texture(
        Device& device,
        const wz::engine::assets::HDRImageData& image);
}
