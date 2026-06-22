#pragma once
// gpu/shader_types.h
#include <cstdint>
#include <span>
#include <string>

namespace wz::gpu
{
    enum class ShaderStage : uint8_t { Vertex, Pixel, Compute };

    struct HLSLCompileDesc {
        ShaderStage stage = ShaderStage::Vertex;
        std::string entry = "main";
        std::string target = "vs_6_0";
        // Index into dep_nodes identifying the primary .hlsl source.
        // Header files typically appear first; this points past them.
        uint32_t primary_source_index = 0;
    };

    // Concatenate HLSL source dependencies in declared order into one
    // translation unit (non-empty sources, newline-separated). The single
    // definition of how multiple .hlsl deps combine, so every compile path —
    // the legacy gpu handle (compile_hlsl) and the rhi ShaderModule (the shader
    // asset compiler) — sees byte-identical source instead of drifting. Mirrors
    // the asset DAG model where source order is meaningful.
    inline std::string combine_hlsl_sources(
        std::span<const std::span<const uint8_t>> sources)
    {
        std::string combined;
        for (std::span<const uint8_t> src : sources) {
            if (src.empty()) {
                continue;
            }
            combined.append(
                reinterpret_cast<const char*>(src.data()), src.size());
            combined.push_back('\n');
        }
        return combined;
    }
}
