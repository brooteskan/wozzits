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

#include <wozzits/rhi/render_program.h>

namespace wz::engine::rendering
{
    // Convert an authored custom render-program description to the rhi contract.
    // Descriptor semantics are resolved into Tags in the provided rhi registry
    // (the open-identity-enum -> registry conversion). Shader AssetKeys are
    // carried as stable string refs for rhi's shader subsystem to resolve.
    wz::rhi::RenderProgramDesc to_rhi_render_program_desc(
        const wz::engine::assets::CustomRenderProgramDesc& src,
        wz::rhi::DescriptorSemanticRegistry& semantics);
}
