// src/engine/assets/inochi/puppet_authoring.cpp

#include <engine/assets/inochi/puppet_authoring.h>

#include <engine/assets/puppet_program.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <cstdint>

namespace wz::engine::assets::inochi
{
    namespace
    {
        namespace authoring = wz::engine::assets::authoring;

        // The shared puppet program is added once per project, so the presence
        // of a (non-deleted) puppet-program node means the whole shared subgraph
        // is already authored.
        wz::asset::AssetGraphDraftNodeId find_puppet_program_node(
            const wz::asset::AssetGraphDraft& draft)
        {
            for (const wz::asset::AssetGraphDraftNode& node : draft.nodes) {
                if (node.state != wz::asset::AssetGraphDraftNodeState::Deleted
                    && node.node.schema.value == kPuppetProgramSchema.value)
                {
                    return node.id;
                }
            }
            return wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        }
    }

    wz::asset::AssetGraphDraftNodeId ensure_shared_puppet_program_node(
        wz::asset::AssetGraphDraft& draft,
        const authoring::GraphAuthoringContext& ctx,
        wz::Logger& logger)
    {
        // Already present -> shared; return it. One program node with many
        // incoming references is the sharing case the draft commit allows.
        if (const wz::asset::AssetGraphDraftNodeId existing =
                find_puppet_program_node(draft);
            existing != wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE)
        {
            return existing;
        }

        // Stage the embedded puppet shaders into <project>/shaders/puppet/ so
        // the ShaderSource file nodes resolve.
        if (!stage_puppet_shaders(logger, ctx.resolve_file)) {
            logger.error(
                "inochi shared assets: failed to stage puppet shaders");
            return wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        }

        // Vertex: HLSL source file -> Shader. The Shader node's stage defaults
        // to Vertex, and the params path derives the vs_5_1 target the puppet's
        // space2 bindings require (no explicit HLSLCompileDesc meta needed).
        const wz::asset::AssetGraphDraftNodeId vs_source =
            authoring::add_source_asset_node(
                draft,
                ctx,
                kHLSLFileSchema,
                wz::asset::AssetType::ShaderSource,
                /*params=*/{},
                wz::fs::Path{ kPuppetVertexShaderProjectPath });
        const wz::asset::AssetGraphDraftNodeId vs_shader =
            authoring::add_source_asset_node(
                draft, ctx, kHLSLShaderSchema, wz::asset::AssetType::Shader);

        // Pixel: HLSL source file -> Shader. Stage = 1 (Pixel) -> ps_5_1.
        wz::asset::ParamBlock ps_params;
        ps_params.values["stage"] = static_cast<int64_t>(1);
        const wz::asset::AssetGraphDraftNodeId ps_source =
            authoring::add_source_asset_node(
                draft,
                ctx,
                kHLSLFileSchema,
                wz::asset::AssetType::ShaderSource,
                /*params=*/{},
                wz::fs::Path{ kPuppetPixelShaderProjectPath });
        const wz::asset::AssetGraphDraftNodeId ps_shader =
            authoring::add_source_asset_node(
                draft,
                ctx,
                kHLSLShaderSchema,
                wz::asset::AssetType::Shader,
                ps_params);

        // The puppet render program (fixed SRG baked by the kPuppetProgramSchema
        // compiler from its two shader deps).
        const wz::asset::AssetGraphDraftNodeId program =
            authoring::add_source_asset_node(
                draft, ctx, kPuppetProgramSchema, kAssetTypeRenderProgram);

        if (vs_source == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
            || vs_shader == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
            || ps_source == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
            || ps_shader == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
            || program == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE)
        {
            logger.error(
                "inochi shared assets: failed to author the puppet program "
                "subgraph nodes");
            return wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        }

        // Edges. Shader compiler port 0 = "source_file"; puppet program ports
        // 0 = "vertex_shader", 1 = "pixel_shader".
        wz::asset::connect_asset_graph_draft_nodes(
            draft, vs_source, vs_shader, 0);
        wz::asset::connect_asset_graph_draft_nodes(
            draft, ps_source, ps_shader, 0);
        wz::asset::connect_asset_graph_draft_nodes(
            draft, vs_shader, program, 0);
        wz::asset::connect_asset_graph_draft_nodes(
            draft, ps_shader, program, 1);

        return program;
    }
}
