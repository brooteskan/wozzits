#pragma once

#include <asset/compiler.h>
#include <gpu/shader_types.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace wz::engine::assets::internal
{
    [[nodiscard]] inline wz::gpu::ShaderStage hlsl_shader_stage_from_index(
        int64_t value)
    {
        if (value == 1) {
            return wz::gpu::ShaderStage::Pixel;
        }
        if (value == 2) {
            return wz::gpu::ShaderStage::Compute;
        }
        return wz::gpu::ShaderStage::Vertex;
    }

    [[nodiscard]] inline std::string default_hlsl_target_for_stage(
        wz::gpu::ShaderStage stage)
    {
        switch (stage) {
        case wz::gpu::ShaderStage::Vertex:
            return "vs_5_1";
        case wz::gpu::ShaderStage::Pixel:
            return "ps_5_1";
        case wz::gpu::ShaderStage::Compute:
            return "cs_5_1";
        }
        return "vs_5_1";
    }

    [[nodiscard]] inline wz::gpu::HLSLCompileDesc hlsl_compile_desc_from_params(
        const wz::asset::ParamBlock& params)
    {
        wz::gpu::HLSLCompileDesc desc{};
        desc.stage =
            hlsl_shader_stage_from_index(params.get<int64_t>("stage", 0));
        desc.entry = params.get<std::string>("entry", "main");
        // Target is DERIVED from stage: stage is the single source of truth, so a
        // node can no longer be authored with a stage/target that disagree (which
        // used to compile against the target while registering under the stage,
        // e.g. a pixel body tagged Vertex). The free-text "target" param is gone
        // from the node's declared params; any stale value in old graph JSON is
        // ignored. (task #1)
        desc.target = default_hlsl_target_for_stage(desc.stage);
        desc.primary_source_index =
            static_cast<uint32_t>(
                std::max<int64_t>(
                    0,
                    params.get<int64_t>("primary_source_index", 0)));
        if (desc.entry.empty()) {
            desc.entry = "main";
        }
        return desc;
    }
}
