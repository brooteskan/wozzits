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

    // Minimal texture format set the engine texture creator supports. Extend as
    // new field / texture asset types come onto the rhi registry (vector field,
    // texture, environment map). Scalar field needs only R32Float.
    enum class TextureFormat : uint8_t { R32Float };

    struct TextureDesc
    {
        TextureDimension dimension = TextureDimension::Texture2D;
        uint32_t         width  = 0;
        uint32_t         height = 0;
        uint32_t         depth  = 1;
        TextureFormat    format = TextureFormat::R32Float;

        bool valid() const noexcept
        {
            if (width == 0u || height == 0u || depth == 0u) {
                return false;
            }
            if (dimension == TextureDimension::Texture2D && depth != 1u) {
                return false;
            }
            return true;
        }
    };

    // Create a committed GPU texture (default heap, ready to receive an upload).
    // No SRV is created. Returns an invalid handle on failure.
    [[nodiscard]] GPUHandle create_texture(Device& device, const TextureDesc& desc);

    // Upload CPU bytes into a texture created by create_texture, transitioning it
    // to a shader-resource state. v1 expects a full-resource write at offset 0;
    // offset is reserved for future sub-resource writes.
    bool update_texture(
        Device& device,
        GPUHandle handle,
        const void* data,
        uint64_t size,
        uint64_t offset = 0);

    bool release_texture(Device& device, GPUHandle handle);
}
