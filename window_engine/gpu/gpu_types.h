#pragma once
// gpu/gpu_types.h

#include <cstdint>
#include <asset/types.h>
namespace wz::gpu
{
    static constexpr uint32_t INVALID_GPU_INDEX = UINT32_MAX;
    static constexpr uint32_t INVALID_GPU_GENERATION = 0;

    using GPUHandle = wz::asset::ResourceHandle;
    using GPUResourceType = wz::asset::AssetType;

    inline constexpr GPUHandle INVALID_GPU_HANDLE{};

}