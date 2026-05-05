#pragma once
// engine/assets/shader/shader_types.h
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
}
