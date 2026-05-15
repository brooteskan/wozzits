#pragma once

// gpu/dx12/dx12_gaussian_splat_debug.h
//
// DX12-only diagnostic Gaussian splat renderer.
//
// This is not the final optimized Gaussian splat path. It draws each splat as a
// small camera-facing debug disc/quad to prove that GaussianSplatCloudData can
// become a visible GPU resource.

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

namespace wz::gpu::dx12
{
    struct GaussianSplatDebugContextDesc
    {
        GPUHandle vertex_shader{};
        GPUHandle pixel_shader{};
        GPUHandle splat_cloud{};

        bool valid() const noexcept
        {
            return vertex_shader.valid()
                && pixel_shader.valid()
                && splat_cloud.valid();
        }
    };

    struct GaussianSplatDebugView
    {
        // Column-major 4x4 matrices, matching the existing debug transform path.
        float world[16]{};
        float view_proj[16]{};

        // x = viewport width
        // y = viewport height
        // z = base splat size in pixels
        // w = unused
        float viewport_and_size[4]{};
    };

    void create_gaussian_splat_debug_context(
        Device& device,
        const GaussianSplatDebugContextDesc& desc);

    void submit_gaussian_splat_debug_frame(
        Device& device,
        const GaussianSplatDebugView& view);
}