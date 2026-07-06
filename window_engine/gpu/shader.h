#pragma once
// gpu/shader.h

#include <gpu/gpu_types.h>
#include <gpu/shader_types.h>

#include <span>
#include <string>
#include <cstdint>

namespace wz::gpu
{
    struct Device;

    // On compile failure, `out_error` (when non-null) receives the compiler's
    // diagnostic text so callers can surface it (log + node inspector) instead of
    // it vanishing into OutputDebugStringA.
    GPUHandle compile_hlsl(
        Device& device,
        std::span<const std::span<const uint8_t>> sources,
        const HLSLCompileDesc& desc,
        std::string* out_error = nullptr
    );
}