#pragma once

// engine/assets/puppet_program.h
//
// Engine-provided render program for Inochi2D puppets (inochi S2c). A puppet is
// a SPECIAL CASE: unlike a mesh renderable, its rendering is DEFINED by the
// format (it should appear as it does in the Inochi editor), so the program is
// never authored by the user. The engine owns the one canonical puppet program
// and provisions it on demand — the puppet renderable only needs a Puppet.
//
// This mirrors ensure_mesh_style_pull_program: the file carrier resolves shader
// paths against the PROJECT root (no engine-resource fallback), so the canonical
// HLSL source is embedded here and staged into <project>/shaders/puppet/ on
// demand (write-if-missing). There is deliberately NO copy under the engine's
// resources/shaders/ — this file is the single source of truth for the puppet
// shaders + their SRG (the SRG here MUST match the shaders, and both match the
// per-Part packet build in rhi_scene_renderer.cpp).
//
// GPU gating: shader assets cannot compile without a device, so callers must
// gate provisioning on device validity — deviceless, a puppet simply gets no
// renderable, matching the app model (and the mesh_style / mesh_vertex_pull
// device gates).
//
// Unlike the mesh-style program there is exactly ONE puppet program (fixed
// pipeline state: MeshVertexPull + AlphaBlend + depth Disabled + SolidCullNone,
// drawn in the Overlay layer), so it dedups to a single asset via create_custom's
// deterministic key. Per-Part Multiply/Screen blend variants + masks are later
// seams (S3/S5/S6); when they land they add MORE engine-owned puppet programs
// selected from puppet data — still never user-authored.

#include <engine/assets/render_program/render_program.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/file_carrier_asset_module.h>

#include <logging/logger.h>

#include <file/filesystem.h>

#include <functional>
#include <string>

namespace wz::engine::assets
{
    // Stages the embedded puppet shader sources into <project>/shaders/puppet/
    // (write-if-missing), registers the shader pair, and creates/dedups the
    // custom render program carrying the fixed puppet SRG (Screen view head at
    // t0/space0; PuppetVertices t0 / PuppetIndices t1 / PuppetAtlas t2 + a
    // LinearClamp sampler s0 in space2; the 16-dword "puppet_part" root-constant
    // block at b0/space2). Idempotent: a second call returns the same asset.
    // Returns an invalid asset on failure (caller logs/skips the renderable).
    RenderProgramAsset ensure_puppet_program(
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        ShaderAssetModule& shaders,
        RenderProgramAssetModule& render_programs);

    // The fixed puppet SRG as a CustomRenderProgramDesc (name set; shaders
    // UNSET — the caller assigns vertex_shader/pixel_shader). Shared by
    // ensure_puppet_program (typed path) and the kPuppetProgramSchema compiler
    // (graph path), so the SRG lives in exactly one place.
    [[nodiscard]] CustomRenderProgramDesc puppet_program_srg_desc(
        const std::string& name);

    // Project-root-relative paths of the canonical puppet shaders. Single source
    // of truth for both the typed ensure_puppet_program() path and the graph-
    // authoring routine (which references them as ShaderSource nodes).
    inline constexpr const char* kPuppetVertexShaderProjectPath =
        "shaders/puppet/puppet_vs.hlsl";
    inline constexpr const char* kPuppetPixelShaderProjectPath =
        "shaders/puppet/puppet_ps.hlsl";

    // Stage the embedded puppet shader sources into <project>/shaders/puppet/
    // (write-if-missing), resolving the project-relative paths via resolve_path.
    // Returns false on an IO failure (directory create / write). Shared by the
    // typed ensure_puppet_program() path (FileCarrierAssetModule::resolve_path)
    // and the graph-authoring routine (GraphAuthoringContext::resolve_file), so
    // the embedded sources live in exactly one place.
    [[nodiscard]] bool stage_puppet_shaders(
        wz::Logger& logger,
        const std::function<wz::fs::Path(const wz::fs::Path&)>& resolve_path);
}
