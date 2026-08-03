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
    //
    // On compile SUCCESS, `out_warnings` (when non-null) receives the same blob
    // when FXC filled one anyway. It is a separate parameter because it is a
    // different event: the shader compiled, and the caller should log rather
    // than fail. Previously the success path released the blob unread, so every
    // warning in the corpus was discarded at the one place that could see it --
    // measured at 12 across 87 shaders, all X4000 and all benign, which is
    // exactly the state in which opening the channel costs nothing. FXC also
    // emits correctness-grade diagnostics here (X4008 division-by-zero, X3206
    // implicit truncation); none are in the corpus today (#316, C3-Q3).
    GPUHandle compile_hlsl(
        Device& device,
        std::span<const std::span<const uint8_t>> sources,
        const HLSLCompileDesc& desc,
        std::string* out_error = nullptr,
        std::string* out_warnings = nullptr
    );
}