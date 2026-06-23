#pragma once

// gpu/gpu_resource_types.h

#include <gpu/gpu_types.h>

namespace wz::gpu
{
    inline constexpr GPUResourceType kGPUBufferResourceType =
        static_cast<GPUResourceType>(515);

    // Mirrors kAssetTypeGPUTexture (518). Tags handles minted by the generic
    // engine texture creator (gpu/texture.h) so the rhi backend can route
    // destroy/write to the texture table rather than the buffer table.
    inline constexpr GPUResourceType kGPUTextureResourceType =
        static_cast<GPUResourceType>(518);

    inline constexpr GPUResourceType kGPUComputePipelineResourceType =
        static_cast<GPUResourceType>(525);

    inline constexpr GPUResourceType kGPUGraphicsPipelineResourceType =
        static_cast<GPUResourceType>(532);

    inline constexpr GPUResourceType kGPUMeshFieldBufferResourceType =
        static_cast<GPUResourceType>(536);
}
