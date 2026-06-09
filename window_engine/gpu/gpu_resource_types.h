#pragma once

// gpu/gpu_resource_types.h

#include <gpu/gpu_types.h>

namespace wz::gpu
{
    inline constexpr GPUResourceType kGPUBufferResourceType =
        static_cast<GPUResourceType>(515);

    inline constexpr GPUResourceType kGPUComputePipelineResourceType =
        static_cast<GPUResourceType>(525);

    inline constexpr GPUResourceType kGPUGraphicsPipelineResourceType =
        static_cast<GPUResourceType>(532);

    inline constexpr GPUResourceType kGPUMeshFieldBufferResourceType =
        static_cast<GPUResourceType>(536);
}
