#pragma once

// engine/rendering/rhi_gpu_backend.h
//
// Engine-side implementation of wz::rhi::GpuBackend (topology A: the engine
// depends on wozzits-rhi). It backs the new renderer's resource registry with
// the engine's existing, working DX12 functions — reusing the hard-won device,
// upload, and release plumbing rather than rewriting D3D12.
//
// rhi stays pure: it only defines the GpuBackend interface. This adapter lives
// engine-side, so there is no rhi -> engine dependency (which would be a cycle,
// since engine -> rhi already exists for the render-program bridge).
//
// Increment 1: the BUFFER resource path, delegating to the existing
// create_structured_buffer / create_rw_structured_buffer / release_compute_buffer.
// Increment 2 (#197): the TEXTURE path (Texture2D/3D), delegating to the generic
// engine texture creator (gpu/texture.h). The CommandRecorder (barriers) is a
// subsequent increment.

#include <gpu/compute.h>
#include <gpu/gpu.h>
#include <gpu/gpu_types.h>
#include <gpu/texture.h>

#include <wozzits/rhi/gpu_resource.h>

#include <cstdint>
#include <unordered_map>

namespace wz::engine::rendering
{
    // True when the resource's usage asks for unordered access (UAV) -> the
    // read-write structured buffer creator. Pure; unit-tested without a device.
    [[nodiscard]] inline bool desc_wants_rw(const wz::rhi::GpuResourceDesc& desc)
    {
        return (desc.usage & wz::rhi::ResourceUsage_Storage) != 0u;
    }

    // Map an rhi buffer desc onto the engine's ComputeBufferDesc. A structured
    // buffer carries its stride; a raw (stride 0) buffer is treated as a
    // tightly-packed 4-byte-element buffer so it still has a valid SRV stride.
    // Pure; unit-tested without a device.
    [[nodiscard]] inline wz::gpu::ComputeBufferDesc to_compute_buffer_desc(
        const wz::rhi::GpuResourceDesc& desc)
    {
        const uint32_t stride = desc.stride_bytes > 0u ? desc.stride_bytes : 4u;
        wz::gpu::ComputeBufferDesc out;
        out.stride_bytes = stride;
        out.element_count = static_cast<uint32_t>(desc.size_bytes / stride);
        out.initial_data = nullptr;
        out.initial_data_bytes = 0;
        return out;
    }

    // Map an rhi texture format onto the engine texture format. Returns false for
    // a format the engine texture creator does not support yet, so create() can
    // reject it cleanly. Pure; unit-tested without a device.
    [[nodiscard]] inline bool to_gpu_texture_format(
        wz::rhi::TextureFormat format, wz::gpu::TextureFormat& out)
    {
        switch (format) {
        case wz::rhi::TextureFormat::R32Float:
            out = wz::gpu::TextureFormat::R32Float;
            return true;
        case wz::rhi::TextureFormat::RGBA8Unorm:
            out = wz::gpu::TextureFormat::RGBA8Unorm;
            return true;
        case wz::rhi::TextureFormat::RGBA16Float:
            out = wz::gpu::TextureFormat::RGBA16Float;
            return true;
        case wz::rhi::TextureFormat::RGBA32Float:
            out = wz::gpu::TextureFormat::RGBA32Float;
            return true;
        case wz::rhi::TextureFormat::RGBA8UnormSrgb:
            out = wz::gpu::TextureFormat::RGBA8UnormSrgb;
            return true;
        default:
            return false;
        }
    }

    // The switch above keeps a default: (Undefined + the depth formats reject
    // cleanly), so an appended wz::rhi::TextureFormat would silently fall to
    // `return false` instead of being routed -- exactly the append-blindness
    // #285 (C3-H5 on #316) calls out. Count pins the extent so a new format
    // trips here at build time first. Bump only after adding the case above.
    static_assert(
        static_cast<int>(wz::rhi::TextureFormat::Count) == 8,
        "wz::rhi::TextureFormat changed -- route the new format in "
        "to_gpu_texture_format() before bumping this expected count.");

    // Map an rhi texture desc onto the engine's TextureDesc (dims + dimension +
    // format). Returns false for an unsupported format. The dx12 layer stays
    // rhi-agnostic; this is where the rhi -> gpu translation lives, mirroring
    // to_compute_buffer_desc for the buffer family. Pure; unit-tested w/o a device.
    [[nodiscard]] inline bool to_texture_desc(
        const wz::rhi::GpuResourceDesc& desc, wz::gpu::TextureDesc& out)
    {
        wz::gpu::TextureFormat format{};
        if (!to_gpu_texture_format(desc.format, format)) {
            return false;
        }
        out.dimension =
            desc.dimension == wz::rhi::ResourceDimension::Texture3D
                ? wz::gpu::TextureDimension::Texture3D
                : wz::gpu::TextureDimension::Texture2D;
        out.width = desc.width;
        out.height = desc.height;
        out.depth = desc.depth;
        out.mip_levels = desc.mip_levels;
        out.format = format;
        // Offscreen render-to-texture (S6): a texture the renderer draws into.
        out.render_target =
            (desc.usage & wz::rhi::ResourceUsage_RenderTarget) != 0u;
        for (int i = 0; i < 4; ++i) {
            out.optimized_clear[i] = desc.optimized_clear[i];
        }
        return true;
    }

    class EngineGpuBackend final : public wz::rhi::GpuBackend
    {
    public:
        // The device must outlive the backend.
        explicit EngineGpuBackend(wz::gpu::Device& device) : device_(&device) {}

        wz::rhi::BackendResource create(
            const wz::rhi::GpuResourceDesc& desc) override;
        void destroy(wz::rhi::BackendResource resource) override;
        bool write(wz::rhi::BackendResource resource,
                   const void* data,
                   uint64_t size,
                   uint64_t offset) override;
        bool write_texture_mip(wz::rhi::BackendResource resource,
                               uint32_t mip_level,
                               const void* data,
                               uint64_t size) override;

        [[nodiscard]] wz::gpu::GPUHandle gpu_handle_for(
            wz::rhi::BackendResource resource) const;

    private:
        // What backs one rhi token: the engine GPUHandle plus the physical
        // family it came from, so destroy()/write() route to the buffer vs.
        // texture path without re-deriving it from the (opaque) handle.
        struct Entry
        {
            wz::gpu::GPUHandle        handle{};
            wz::rhi::ResourceDimension dimension =
                wz::rhi::ResourceDimension::Buffer;
        };

        wz::gpu::Device* device_;

        // Opaque rhi token -> engine resource. The token is the backend's own
        // counter; the full GPUHandle (id + epoch + type) is preserved here
        // rather than lossily packed into the 64-bit token.
        std::unordered_map<uint64_t, Entry> resources_;
        uint64_t next_token_ = 1;
    };
}
