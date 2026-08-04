#pragma once

// gpu/texture.h
//
// Generic, asset-agnostic GPU texture creation + upload. This is the
// generalization of what dx12_scalar_field_texture.cpp did for one field type:
// it creates a committed texture and uploads CPU bytes into it, with NO SRV /
// descriptor created here (descriptor binding is a bind-time / SRG concern owned
// by the renderer). The engine-side rhi backend (engine/rendering/rhi_gpu_backend)
// maps a wz::rhi::GpuResourceDesc onto TextureDesc so the gpu/dx12 layer stays
// rhi-agnostic, mirroring how create_structured_buffer backs the rhi buffer path.

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <cstdint>

namespace wz::gpu
{
    // Physical texture family. Mirrors the rhi ResourceDimension texture cases.
    enum class TextureDimension : uint8_t { Texture2D, Texture3D };

    // Texture format set the engine texture creator supports. Extend as new
    // field / texture asset types come onto the rhi registry. Scalar field needs
    // only R32Float; vector field / texture / environment-map residency (#201)
    // needs the RGBA formats (float32 for HDR / precise vector data, float16 and
    // unorm8 for compact image data). Each entry pairs a DXGI format and a texel
    // byte count in dx12_texture.cpp.
    enum class TextureFormat : uint8_t
    {
        R32Float,
        RGBA8Unorm,
        RGBA16Float,
        RGBA32Float,
        // Same bytes as RGBA8Unorm; the sampler decodes sRGB -> linear on read.
        // Appended to keep prior ordinals stable. Pairs with a DXGI _SRGB format
        // in dx12_texture.cpp. Honours the authored TextureColorSpace (#324).
        RGBA8UnormSrgb,
        // Keep last: pins the enum extent for the to_dxgi_format guard. #324.
        Count,
    };

    // Full mip count for a texture of the given 2D dimensions:
    // floor(log2(max(w,h))) + 1. The value mip_levels may not exceed.
    [[nodiscard]] inline uint32_t max_mip_levels(uint32_t width, uint32_t height) noexcept
    {
        uint32_t largest = width > height ? width : height;
        uint32_t levels = 1u;
        while (largest > 1u) {
            largest >>= 1u;
            ++levels;
        }
        return levels;
    }

    struct TextureDesc
    {
        TextureDimension dimension = TextureDimension::Texture2D;
        uint32_t         width  = 0;
        uint32_t         height = 0;
        uint32_t         depth  = 1;
        // Number of mip levels in the chain. 1 == single full-res surface (the
        // pre-#209 default). Must be within [1, max_mip_levels(width,height)];
        // an out-of-range value is a validation failure (see valid()). Texture3D
        // supports only mip_levels == 1 for now (rejected otherwise).
        uint32_t         mip_levels = 1;
        TextureFormat    format = TextureFormat::R32Float;
        // When true the texture is created renderable: D3D12 ALLOW_RENDER_TARGET +
        // an owned RTV, so a pass can draw into it, and it rests SRV-readable so it
        // can then be sampled. Set from rhi ResourceUsage_RenderTarget. Offscreen
        // render-to-texture (S6). mip_levels must be 1 for a render target.
        bool             render_target = false;

        bool valid() const noexcept
        {
            if (width == 0u || height == 0u || depth == 0u) {
                return false;
            }
            if (dimension == TextureDimension::Texture2D && depth != 1u) {
                return false;
            }
            if (mip_levels == 0u) {
                return false;
            }
            // Texture3D mip chains are not supported yet: reject rather than
            // silently create wrong state.
            if (dimension == TextureDimension::Texture3D && mip_levels != 1u) {
                return false;
            }
            if (mip_levels > max_mip_levels(width, height)) {
                return false;
            }
            // A render target is a single full-res surface (no mip chain to draw
            // into) and 2D only.
            if (render_target
                && (mip_levels != 1u
                    || dimension != TextureDimension::Texture2D))
            {
                return false;
            }
            return true;
        }
    };

    // Create a committed GPU texture (default heap, ready to receive an upload).
    // No SRV is created. Returns an invalid handle on failure.
    [[nodiscard]] GPUHandle create_texture(Device& device, const TextureDesc& desc);

    // Upload CPU bytes into a texture created by create_texture, transitioning it
    // to a shader-resource state. Expects a full-resolution (mip 0) write at
    // offset 0; offset is reserved for future sub-resource writes. Equivalent to
    // update_texture_mip(device, handle, 0, data, size).
    bool update_texture(
        Device& device,
        GPUHandle handle,
        const void* data,
        uint64_t size,
        uint64_t offset = 0);

    // Upload CPU bytes into one mip level of a texture. `mip_level` is 0-based
    // (0 == the full-resolution surface). `data` is tightly packed for that
    // level's dimensions — max(width>>mip, 1) x max(height>>mip, 1) x depth —
    // with no row padding; this honors the device row pitch internally.
    // `size` must equal the level's tightly-packed footprint. Returns false for
    // an out-of-range mip level or a mismatched size. The texture is left in a
    // shader-resource state.
    bool update_texture_mip(
        Device& device,
        GPUHandle handle,
        uint32_t mip_level,
        const void* data,
        uint64_t size);

    bool release_texture(Device& device, GPUHandle handle);
}
