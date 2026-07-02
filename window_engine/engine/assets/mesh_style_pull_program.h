#pragma once

// engine/assets/mesh_style_pull_program.h
//
// Shared provisioning for the styled RHI pull-mesh render program (issue #195
// slices A+B). This is what replaced the legacy 0x700 wireframe / 0x705 styled
// renderable schemas: a mesh renderable is ONE recipe (0x706) = mesh + render
// program + optional MeshRenderStyle, where the wireframe-vs-solid look is a
// PROGRAM property (RasterMode) and the style's shading constants flow as data
// (the 28-dword "mesh_style" root-constant block, binding_layout==4 shape).
//
// Both authoring paths that used to call create_mesh_styled — the GLB scene
// import (scene_asset_module.cpp) and scene materialization
// (scene_authoring_materialize.cpp) — provision their program through
// ensure_mesh_style_pull_program below.
//
// Shader staging: HLSL sources resolve against the PROJECT root (the file
// carrier has no engine-resource fallback), so the canonical shader source is
// embedded here and written into <project>/shaders/mesh_style/ on demand
// (write-if-missing). There is deliberately NO copy under the engine's
// resources/shaders/ — this header is the single source of truth.
//
// GPU gating: shader assets cannot compile without a GPU device
// (wz::gpu::compile_hlsl requires one), so callers must gate provisioning on
// device validity — deviceless, a mesh node simply gets no renderable, which
// matches the app model (a node with geometry but no program does not draw)
// and the pre-existing device gate on the mesh_vertex_pull program.

#include <engine/assets/mesh_render_style/mesh_render_style.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/file_carrier_asset_module.h>

#include <logging/logger.h>

namespace wz::engine::assets
{
    // Stages the embedded shader sources into <project>/shaders/mesh_style/
    // (write-if-missing), registers the shader pair, and creates/dedups the
    // custom render program whose pipeline state is derived from the style:
    //   raster : wireframe-only style -> WireframeCullNone,
    //            else double_sided ? SolidCullNone : SolidCullBack
    //   depth  : depth_test ? (depth_write ? TestWrite : TestNoWrite) : Disabled
    //   blend  : transparent style (alpha) -> AlphaBlend, else Opaque
    // The program name folds the derived state, so styles that share pipeline
    // state share one program asset (create_custom dedups by key), while the
    // per-style COLOURS flow per-renderable via the recipe's baked shading.
    // Returns an invalid asset on failure (caller logs/skips the renderable).
    RenderProgramAsset ensure_mesh_style_pull_program(
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        ShaderAssetModule& shaders,
        RenderProgramAssetModule& render_programs,
        const MeshRenderStyleData& style);
}
