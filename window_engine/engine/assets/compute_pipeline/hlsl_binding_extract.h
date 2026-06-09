#pragma once

// engine/assets/compute_pipeline/hlsl_binding_extract.h

#include <engine/assets/compute_pipeline/compute_pipeline.h>
#include <engine/behavior/behavior_plugin_abi.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wz::engine::assets
{
    enum class HlslBindingPortTarget : uint8_t
    {
        Buffer,
        RootConstant,
    };

    struct HlslBindingPort
    {
        std::string name;
        WzGpuPortKind port_kind = WZ_GPU_PORT_NONE;
        WzGpuPortDirection direction = 0u;
        HlslBindingPortTarget target = HlslBindingPortTarget::Buffer;
        ComputeBindingKind binding_kind = ComputeBindingKind::StructuredBufferSRV;
        uint32_t shader_register = 0;
        uint32_t register_space = 0;
        uint32_t stride_bytes = 0;
        uint32_t root_constant_offset = 0;
        uint32_t root_constant_dwords = 0;
    };

    struct HlslBindingExtraction
    {
        std::vector<HlslBindingPort> ports;
        std::vector<std::string> diagnostics;
        uint32_t root_constant_dwords = 0;

        [[nodiscard]] bool ok() const noexcept
        {
            return diagnostics.empty();
        }
    };

    [[nodiscard]] std::string normalize_hlsl_port_name(std::string_view name);

    [[nodiscard]] HlslBindingExtraction extract_hlsl_bindings_from_source(
        std::string_view source);

    [[nodiscard]] HlslBindingExtraction extract_hlsl_bindings_from_file(
        const std::string& path);
}
