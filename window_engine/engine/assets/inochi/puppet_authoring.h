#pragma once

// engine/assets/inochi/puppet_authoring.h
//
// Graph-authoring of the shared "Inochi shared assets" subgraph (inochi S2c).
//
// A puppet renderable requires a puppet render program, but that program is a
// FIXED engine recipe (kPuppetProgramSchema, SRG baked by its compiler) that the
// user never authors. It also cannot be minted per-puppet: the draft commit
// (AssetSystem::replace_registered_assets) rejects duplicate keys, so N puppets
// each creating their own program would collide. The resolution is a SHARED
// program node added ONCE per project, that many puppet renderables reference by
// an explicit edge (one node, many edges is the case the commit allows).
//
// This routine find-or-creates that shared subgraph in an AssetGraphDraft:
//
//     VS source ─▶ VS shader ─▶ PuppetProgram (port 0 "vertex_shader")
//     PS source ─▶ PS shader ─▶ PuppetProgram (port 1 "pixel_shader")
//
// (5 nodes, 4 edges). The shaders are the engine's embedded puppet HLSL, staged
// into <project>/shaders/puppet/ on demand; the Shader nodes author via the
// params path, whose stage-derived target is vs_5_1/ps_5_1 — the Shader Model
// 5.1 the puppet's space2 bindings require. It returns the PuppetProgram node id
// so the caller can wire each puppet renderable's program port to it.

#include <asset/draft.h>
#include <engine/assets/authoring/asset_graph_authoring.h>

#include <logging/logger.h>

namespace wz::engine::assets::inochi
{
    // Find-or-create the shared puppet-program subgraph in `draft`. If a puppet-
    // program node already exists (the subgraph was added earlier), returns its
    // id without adding anything. Otherwise stages the puppet shaders via
    // ctx.resolve_file and authors the 5-node/4-edge subgraph. Returns the
    // PuppetProgram draft node id, or INVALID_ASSET_GRAPH_DRAFT_NODE on failure
    // (staging IO error or a node that failed to author).
    wz::asset::AssetGraphDraftNodeId ensure_shared_puppet_program_node(
        wz::asset::AssetGraphDraft& draft,
        const wz::engine::assets::authoring::GraphAuthoringContext& ctx,
        wz::Logger& logger);
}
