#pragma once
// gpu/shader.h

#include <gpu/gpu_types.h>
#include <engine/assets/shader/shader_types.h>

#include <span>
#include <cstdint>

namespace wz::gpu
{
    struct Device;

    GPUHandle compile_hlsl(
        Device& device,
        std::span<const std::span<const uint8_t>> sources,
        const HLSLCompileDesc& desc
    );
}