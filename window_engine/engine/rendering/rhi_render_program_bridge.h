#pragma once

// engine/rendering/rhi_render_program_bridge.h
//
// Engine -> wozzits-rhi adapter. Converts an authored engine render-program
// description into the rhi boundary contract (wz::rhi::RenderProgramDesc), so
// the new renderer can consume the engine's programs read-only.
//
// This is the engine-side half of the bridge (topology decision A: the engine
// depends on wozzits-rhi). It is the ONLY place that knows both type worlds —
// wozzits-rhi knows nothing of the engine. The two thesis conversions live
// here: the engine's DescriptorSemantic enum becomes a registered rhi Tag, and
// the closed pipeline-state enums map 1:1 (via exhaustiveness-checked switches,
// so a new engine enum member breaks this file at compile time).

#include <engine/assets/render_program/render_program.h>

#include <logging/logger.h>

#include <wozzits/rhi/render_program.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wz::engine::rendering
{
    std::string shader_ref(const wz::asset::AssetKey& key);

    // Deterministic rhi program name derived from the render-program AssetKey
    // (label#hash, like shader_ref). The asset compiler registers the rhi
    // program under this name and the renderer looks it up by it, so producer
    // and consumer agree without sharing the authored name.
    std::string program_ref(const wz::asset::AssetKey& key);

    // D3DCompile HLSL source -> bytecode for an rhi ShaderModule. Shared by the
    // render-program compiler (produces the rhi shader during resolve) and the
    // renderer's render-time fallback. rhi stores opaque bytecode and never
    // compiles source, so this engine-side step feeds it. nullopt on failure.
    std::optional<std::vector<uint8_t>> compile_hlsl_bytecode(
        std::span<const uint8_t> source,
        std::string_view entry,
        const char* target,
        wz::Logger& logger);

    // Why to_rhi_render_program_desc would refuse a program, as a sentence, or
    // an empty string when nothing here would refuse it.
    //
    // Purely diagnostic: the converters still just return nullopt. It exists
    // because "desc conversion failed" sent debugging at shaders, bindings and
    // the asset graph when the actual cause was a root-constant block with no
    // NAME -- one unset string, in producers that otherwise look complete
    // (#317). Both call sites hold these spans already.
    std::string render_program_bridge_refusal(
        std::span<const wz::engine::assets::RootConstantBinding> root_constants,
        std::span<const wz::engine::assets::DescriptorBinding>
            descriptor_bindings);

    // Convert an authored custom render-program description to the rhi contract.
    // Descriptor semantics are resolved into Tags in the provided rhi registry
    // (the open-identity-enum -> registry conversion). Shader AssetKeys are
    // carried as stable string refs for rhi's shader subsystem to resolve.
    std::optional<wz::rhi::RenderProgramDesc> to_rhi_render_program_desc(
        const wz::engine::assets::CustomRenderProgramDesc& src,
        wz::rhi::DescriptorSemanticRegistry& descriptors,
        wz::rhi::ConstantSemanticRegistry& constants);

    // Convert a resolved render-program table entry plus its shader AssetKeys.
    // The table owns declarative state; the graph program node owns shader deps.
    std::optional<wz::rhi::RenderProgramDesc> to_rhi_render_program_desc(
        const wz::engine::assets::RenderProgramData& data,
        const wz::asset::AssetKey& program_key,
        const wz::asset::AssetKey& vertex_key,
        const wz::asset::AssetKey& pixel_key,
        wz::rhi::DescriptorSemanticRegistry& descriptors,
        wz::rhi::ConstantSemanticRegistry& constants);
}
