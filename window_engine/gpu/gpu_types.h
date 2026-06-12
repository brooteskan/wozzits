#pragma once
// gpu/gpu_types.h

#include <cstdint>
#include <string>
#include <asset/types.h>
namespace wz::gpu
{
    static constexpr uint32_t INVALID_GPU_INDEX = UINT32_MAX;
    static constexpr uint32_t INVALID_GPU_GENERATION = 0;

    using GPUHandle = wz::asset::ResourceHandle;
    using GPUResourceType = wz::asset::AssetType;

    inline constexpr GPUHandle INVALID_GPU_HANDLE{};

    enum class DeviceStatus : uint8_t
    {
        Invalid,
        Ok,
        Lost,
    };

    struct DeviceLostInfo
    {
        int32_t operation_hr = 0;
        int32_t removed_reason = 0;
        std::string operation;
        std::string message;
    };

}
