#pragma once

// engine/assets/rhi_shader_source.h
//
// Reading a shader asset's HLSL source bytes + entry point for the rhi path.
// A Shader node's compiled payload is a gpu handle, not source; its source
// lives in its ShaderSource file-carrier dependency. Both the renderer (its
// render-time fallback) and the render-program compiler (via a provider, since
// it has no AssetSystem access of its own) need these bytes to D3DCompile the
// rhi ShaderModule, so the read is centralized on EngineAssetLibrary.

#include <asset/types.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace wz::engine::assets
{
    struct RhiShaderSource
    {
        // Borrows the file-carrier node's payload bytes (stable while that node
        // is resident in the asset system) — consume synchronously.
        std::span<const uint8_t> bytes;
        std::string              entry = "main";
    };

    // Yields a shader's HLSL source + entry by its AssetKey. The library binds
    // this to EngineAssetLibrary::rhi_shader_source() and hands it to the
    // render-program compiler so the compiler can read shader deps during
    // resolve without depending on the AssetSystem directly.
    using RhiShaderSourceProvider =
        std::function<std::optional<RhiShaderSource>(const wz::asset::AssetKey&)>;
}
