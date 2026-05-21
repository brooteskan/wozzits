#pragma once

// gpu/gaussian_splat.h
//
// Public GPU Gaussian splat cloud upload API.
//
// This is the boundary between CPU-side GaussianSplatCloudData assets and
// backend-owned GPU splat buffers. The caller provides CPU splat data; the GPU
// backend owns the resulting buffer and returns an opaque GPUHandle.

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>

namespace wz::gpu
{
    struct GaussianSplatCloudUploadDesc
    {
        const wz::engine::assets::GaussianSplatCloudData* cloud = nullptr;

        // Optional derived color LOD data.  When non-null, its per-splat
        // neighborhood color + confidence is packed into the vertex's
        // lod_color_confidence_rgba8 slot.  When null, that slot is filled
        // from the base color with confidence = 0 so the shader sees a
        // safe fallback.
        const wz::engine::assets::GaussianSplatColorLODData* color_lod = nullptr;

        bool valid() const noexcept
        {
            return cloud != nullptr && cloud->valid();
        }
    };

    [[nodiscard]] GPUHandle upload_gaussian_splat_cloud(
        Device& device,
        const GaussianSplatCloudUploadDesc& desc);

    [[nodiscard]] inline GPUHandle upload_gaussian_splat_cloud(
        Device& device,
        const wz::engine::assets::GaussianSplatCloudData& cloud)
    {
        return upload_gaussian_splat_cloud(device, GaussianSplatCloudUploadDesc{
            .cloud = &cloud,
            });
    }

    [[nodiscard]] inline GPUHandle upload_gaussian_splat_cloud(
        Device& device,
        const wz::engine::assets::GaussianSplatCloudData& cloud,
        const wz::engine::assets::GaussianSplatColorLODData& color_lod)
    {
        return upload_gaussian_splat_cloud(device, GaussianSplatCloudUploadDesc{
            .cloud     = &cloud,
            .color_lod = &color_lod,
            });
    }
}