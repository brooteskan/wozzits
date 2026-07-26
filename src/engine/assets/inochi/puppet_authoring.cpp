// src/engine/assets/inochi/puppet_authoring.cpp

#include <engine/assets/inochi/puppet_authoring.h>

#include <engine/assets/puppet_program.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wz::engine::assets::inochi
{
    namespace
    {
        namespace authoring = wz::engine::assets::authoring;

        // The puppet-program nodes already in the draft, paired with the blend
        // variant each one is. A node authored before #274 carries no blend
        // param and is the Normal variant, matching how the compiler reads it.
        struct ExistingProgramNode
        {
            wz::asset::AssetGraphDraftNodeId id{};
            PuppetProgramBlend blend = PuppetProgramBlend::Normal;
        };

        std::vector<ExistingProgramNode> find_puppet_program_nodes(
            const wz::asset::AssetGraphDraft& draft)
        {
            std::vector<ExistingProgramNode> out;
            for (const wz::asset::AssetGraphDraftNode& node : draft.nodes) {
                if (node.state == wz::asset::AssetGraphDraftNodeState::Deleted
                    || node.node.schema.value != kPuppetProgramSchema.value)
                {
                    continue;
                }
                ExistingProgramNode found{};
                found.id = node.id;
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&node.node.meta))
                {
                    const int64_t raw = params->get<int64_t>(
                        kPuppetProgramBlendParam,
                        static_cast<int64_t>(PuppetProgramBlend::Normal));
                    if (raw >= 0
                        && raw < static_cast<int64_t>(kPuppetProgramBlendCount))
                    {
                        found.blend = static_cast<PuppetProgramBlend>(raw);
                    }
                }
                out.push_back(found);
            }
            return out;
        }

        // The node feeding `to`'s input port `port`, or INVALID.
        wz::asset::AssetGraphDraftNodeId source_at_port(
            const wz::asset::AssetGraphDraft& draft,
            wz::asset::AssetGraphDraftNodeId to,
            std::uint32_t port)
        {
            for (const wz::asset::AssetGraphDraftEdge& edge : draft.edges) {
                if (edge.to == to && edge.to_input_port == port) {
                    return edge.from;
                }
            }
            return wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        }

        // Add the program node for one blend variant and wire it to the shared
        // shader pair. Ports: 0 = vertex_shader, 1 = pixel_shader.
        wz::asset::AssetGraphDraftNodeId add_variant_program_node(
            wz::asset::AssetGraphDraft& draft,
            const authoring::GraphAuthoringContext& ctx,
            PuppetProgramBlend blend,
            wz::asset::AssetGraphDraftNodeId vs_shader,
            wz::asset::AssetGraphDraftNodeId ps_shader)
        {
            wz::asset::ParamBlock params;
            params.values[kPuppetProgramBlendParam] =
                static_cast<int64_t>(blend);
            const wz::asset::AssetGraphDraftNodeId program =
                authoring::add_source_asset_node(
                    draft,
                    ctx,
                    kPuppetProgramSchema,
                    kAssetTypeRenderProgram,
                    params);
            if (program == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE) {
                return program;
            }
            wz::asset::connect_asset_graph_draft_nodes(
                draft, vs_shader, program, 0);
            wz::asset::connect_asset_graph_draft_nodes(
                draft, ps_shader, program, 1);
            return program;
        }
    }

    wz::asset::AssetGraphDraftNodeId ensure_shared_puppet_program_node(
        wz::asset::AssetGraphDraft& draft,
        const authoring::GraphAuthoringContext& ctx,
        wz::Logger& logger)
    {
        // Already present -> shared; return it. One program node with many
        // incoming references is the sharing case the draft commit allows.
        // A graph authored before #274 has only the Normal node, so top up the
        // missing blend variants onto its existing shader pair rather than
        // duplicating the whole subgraph.
        const std::vector<ExistingProgramNode> existing =
            find_puppet_program_nodes(draft);
        if (!existing.empty()) {
            // Prefer the Normal node as THE puppet program; a graph whose blend
            // params did not survive (a host whose registry has no puppet
            // compiler, so nodes carry no ParamBlock at all) reads every node as
            // Normal, and the first is as good as any.
            const auto normal_it = std::ranges::find(
                existing, PuppetProgramBlend::Normal, &ExistingProgramNode::blend);
            const wz::asset::AssetGraphDraftNodeId normal =
                normal_it != existing.end() ? normal_it->id : existing.front().id;

            // Top up ONLY while the set is provably short. Guarding on the count
            // rather than on which blends were observed keeps this idempotent
            // even when the blend params are unreadable -- otherwise every call
            // would re-add the variants it could not recognise and the graph
            // would grow without bound.
            if (existing.size() < kPuppetProgramBlendCount) {
                const wz::asset::AssetGraphDraftNodeId vs_shader =
                    source_at_port(draft, normal, 0);
                const wz::asset::AssetGraphDraftNodeId ps_shader =
                    source_at_port(draft, normal, 1);
                if (vs_shader != wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
                    && ps_shader != wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE)
                {
                    for (std::size_t i = 0; i < kPuppetProgramBlendCount; ++i) {
                        const auto blend = static_cast<PuppetProgramBlend>(i);
                        if (std::ranges::find(
                                existing, blend, &ExistingProgramNode::blend)
                            != existing.end())
                        {
                            continue;
                        }
                        if (add_variant_program_node(
                                draft, ctx, blend, vs_shader, ps_shader)
                            == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE)
                        {
                            logger.error(
                                "inochi shared assets: failed to author a "
                                "puppet program blend variant");
                        }
                    }
                }
            }
            return normal;
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

        if (vs_source == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
            || vs_shader == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
            || ps_source == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
            || ps_shader == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE)
        {
            logger.error(
                "inochi shared assets: failed to author the puppet program "
                "subgraph nodes");
            return wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        }

        // Shader edges. Shader compiler port 0 = "source_file".
        wz::asset::connect_asset_graph_draft_nodes(
            draft, vs_source, vs_shader, 0);
        wz::asset::connect_asset_graph_draft_nodes(
            draft, ps_source, ps_shader, 0);

        // One puppet render program per blend variant (#274), all sharing the
        // single shader pair -- the fixed SRG is baked by the
        // kPuppetProgramSchema compiler, which reads each node's blend_mode
        // param. Normal is returned as THE puppet program (what a renderable
        // binds); the renderer finds the siblings via puppet_program_variants.
        wz::asset::AssetGraphDraftNodeId normal =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        for (std::size_t i = 0; i < kPuppetProgramBlendCount; ++i) {
            const auto blend = static_cast<PuppetProgramBlend>(i);
            const wz::asset::AssetGraphDraftNodeId program =
                add_variant_program_node(
                    draft, ctx, blend, vs_shader, ps_shader);
            if (program == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE) {
                logger.error(
                    "inochi shared assets: failed to author the puppet program "
                    "subgraph nodes");
                return wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
            }
            if (blend == PuppetProgramBlend::Normal) {
                normal = program;
            }
        }
        return normal;
    }
}
