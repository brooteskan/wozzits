// src/engine/app/wozzits_app_v1.cpp

#include <engine/app/wozzits_app_v1.h>

#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene_asset_module.h>

#include <asset/system.h>

#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>
#include <file/filesystem.h>
#include <math/math_types.h>
#include <math/mat4.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace wz::app
{
    namespace
    {
        std::string read_text_file(const wz::fs::Path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                return {};
            }
            return std::string(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
        }

        // A provisional fixed camera until scene cameras are wired: a left-handed
        // perspective with the eye backed well off the origin, far plane large
        // enough for the (heavily scaled) test mesh.
        wz::math::Mat4 default_view_projection()
        {
            constexpr float aspect = 1280.0f / 720.0f;
            const float ys = 1.0f / std::tan((60.0f * 3.1415926535f / 180.0f) * 0.5f);
            wz::math::Mat4 proj{};
            proj.m[0] = ys / aspect;
            proj.m[5] = ys;
            proj.m[10] = 100000.0f / (100000.0f - 1.0f);
            proj.m[11] = 1.0f;
            proj.m[14] = (-1.0f * 100000.0f) / (100000.0f - 1.0f);

            wz::math::Mat4 view = wz::math::Mat4::identity();
            view.m[14] = 5000.0f;  // eye backed off along -Z (LH, looking +Z)
            return wz::math::mul(proj, view);
        }
    }

    WozzitsApp_v1::WozzitsApp_v1(wz::engine::AppContext& ctx)
        : ctx_(ctx)
        , renderer_(ctx.device, ctx.logger)
    {
    }

    uint32_t WozzitsApp_v1::bridge_scene_renderables(
        const wz::asset::AssetGraphDraft& draft)
    {
        // Bridge each scene node's authored renderable graph-node id to its
        // resolved AssetKey (the draft node's key), like the editor's
        // resolve_renderable_asset_node, so the renderer can find it. Re-run on
        // every (re)bind: a graph swap mints new keys, and scene_nodes_ must
        // point at the new ones or the renderer draws nothing (or stale keys).
        uint32_t bridged = 0;
        for (wz::engine::assets::SceneNodeAsset& node : scene_nodes_) {
            if (!node.renderable_asset_node_id) {
                continue;
            }
            // Clear first: if the new graph removed/renamed this authored
            // renderable, the node must stop drawing the previous graph's key
            // (old compiled payloads can still resolve), not keep it stale.
            node.renderable_asset.reset();
            const auto it = draft.node_index.find(*node.renderable_asset_node_id);
            if (it != draft.node_index.end()) {
                node.renderable_asset = draft.nodes[it->second].node.key;
                ++bridged;
            }
        }
        return bridged;
    }

    AssetGraphCompileResult WozzitsApp_v1::bind_asset_graph(
        wz::asset::AssetGraphDraft& draft)
    {
        AssetGraphCompileResult result;

        if (!ctx_.assets) {
            ctx_.logger.error("bind_asset_graph: no asset library");
            return result;
        }
        wz::asset::AssetSystem& sys = ctx_.assets->system();

        // Compiling resolves keys + writes validation messages INTO the draft
        // (AssetGraphDraft is move-only; the caller keeps it and reads status).

        if (!wz::asset::materialize_asset_graph_draft_keys(
                draft, sys.registry(), ctx_.assets->draft_key_factory()))
        {
            result.ok = false;
            result.diagnostics = draft.validation_messages;
            return result;
        }

        const std::vector<wz::asset::AssetGraphDraftRegistration> registrations =
            wz::asset::asset_graph_draft_to_registrations(
                draft, &sys.registry(), ctx_.assets->draft_key_factory());

        std::vector<wz::asset::AssetSystem::RegistrationEntry> entries;
        entries.reserve(registrations.size());
        for (const auto& registration : registrations) {
            entries.push_back(wz::asset::AssetSystem::RegistrationEntry{
                .node = registration.node,
                .dep_keys = registration.dep_keys,
            });
        }

        // The wholesale swap: replace the registered set with the new graph.
        if (!sys.replace_registered_assets(std::move(entries))) {
            result.ok = false;
            result.diagnostics = draft.validation_messages;
            result.diagnostics.push_back(wz::asset::AssetGraphDraftValidationMessage{
                .severity = wz::asset::AssetGraphDraftValidationSeverity::Error,
                .message = "asset graph registration rejected",
            });
            return result;
        }

        // Rebuild the DAG with the newly registered set (commit() only
        // rebuilds the graph; it does NOT run compilers).
        if (!sys.commit()) {
            result.ok = false;
            result.diagnostics = draft.validation_messages;
            result.diagnostics.push_back(wz::asset::AssetGraphDraftValidationMessage{
                .severity = wz::asset::AssetGraphDraftValidationSeverity::Error,
                .message = "asset graph commit (DAG rebuild) rejected",
            });
            return result;
        }

        // Resolve: the actual compile pass — run every node's compiler, filling
        // ResourceHandles. Failures (missing files, compiler errors) surface here.
        std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>>
            resolve_errors;
        sys.resolve_all(&resolve_errors);
        if (!resolve_errors.empty()) {
            result.ok = false;
            result.diagnostics = draft.validation_messages;
            for (const auto& [key, error] : resolve_errors) {
                (void)key;
                result.diagnostics.push_back(
                    wz::asset::AssetGraphDraftValidationMessage{
                        .severity =
                            wz::asset::AssetGraphDraftValidationSeverity::Error,
                        .message = "asset resolve failed (ResolveError "
                            + std::to_string(static_cast<int>(error)) + ")",
                    });
            }
            return result;
        }

        graph_epoch_ = sys.registration_epoch();

        // Rebind the renderer to the new graph: invalidate the realized caches
        // (keyed by the OUTGOING graph's AssetKeys) and deferred-release the
        // outgoing graph's GPU resources, so renderables re-realize against the
        // freshly committed keys instead of drawing the previous graph.
        renderer_.on_graph_changed();

        // The swap minted new AssetKeys; re-point the scene's renderables at
        // them. (On the first bind during load_project the scene is not loaded
        // yet, so this is a no-op there and load_project re-runs it after.)
        bridge_scene_renderables(draft);

        result.ok = true;
        result.diagnostics = draft.validation_messages;
        return result;
    }

    bool WozzitsApp_v1::adopt_asset_graph(wz::asset::AssetGraph&& /*resolved*/)
    {
        // TODO(next): adopt an already-resolved DAG (async/baked handoff). Needs
        // an AssetSystem entry to install a pre-resolved graph; not yet wired.
        ctx_.logger.warn("adopt_asset_graph: not yet implemented");
        return false;
    }

    bool WozzitsApp_v1::load_project(const wz::fs::Path& project)
    {
        if (!ctx_.assets) {
            ctx_.logger.error("load_project: no asset library");
            return false;
        }
        wz::asset::AssetSystem& sys = ctx_.assets->system();

        // Project paths are resource-root-relative; resolve_path prefixes
        // resource_root (e.g. "resources/") for our own direct file reads, and
        // create_scene_from_json does the same internally for the scene.
        const std::string project_text =
            read_text_file(ctx_.assets->files().resolve_path(project));
        if (project_text.empty()) {
            ctx_.logger.error("load_project: cannot read project: " + project);
            return false;
        }
        const wz::json::JSONParseResult pj =
            wz::json::parse_json_string(project_text);
        if (!pj.ok || !pj.document.root) {
            ctx_.logger.error("load_project: invalid project json");
            return false;
        }
        const wz::json::JSONValue& proot = *pj.document.root;
        const wz::fs::Path dir = wz::fs::parent_path(project);

        // Compile the project's asset graph (the unproven path): read the v2
        // graph JSON -> draft (shared engine loader) -> bind (swap + resolve).
        bool graph_ok = false;
        if (const auto graph_rel = wz::json::read_string(proot, "asset_graph")) {
            const wz::fs::Path graph_path =
                wz::fs::join(dir, std::string(*graph_rel));
            const std::string graph_text =
                read_text_file(ctx_.assets->files().resolve_path(graph_path));
            if (graph_text.empty()) {
                ctx_.logger.error(
                    "load_project: cannot read asset graph: " + graph_path);
                return false;
            }
            const wz::json::JSONParseResult gj =
                wz::json::parse_json_string(graph_text);
            if (!gj.ok || !gj.document.root) {
                ctx_.logger.error("load_project: invalid asset graph json");
                return false;
            }

            graph_draft_ = wz::asset::AssetGraphDraft{};
            std::string error;
            if (!wz::engine::assets::load_asset_graph_draft_from_v2_json(
                    *gj.document.root, graph_draft_, error))
            {
                ctx_.logger.error(
                    "load_project: asset graph parse failed: " + error);
                return false;
            }

            const AssetGraphCompileResult bound = bind_asset_graph(graph_draft_);
            graph_ok = bound.ok;
            ctx_.logger.info(
                "load_project: asset graph "
                + std::string(bound.ok ? "compiled" : "FAILED")
                + " (registered="
                + std::to_string(
                    ctx_.assets->system().registered_assets().size())
                + ", diagnostics="
                + std::to_string(bound.diagnostics.size()) + ")");
            for (const auto& d : bound.diagnostics) {
                if (d.severity
                    == wz::asset::AssetGraphDraftValidationSeverity::Error)
                {
                    ctx_.logger.error("load_project:   " + d.message);
                }
            }
            if (!bound.ok) {
                return false;
            }
        }

        // Load + resolve the scene. The path stays resource-root-relative;
        // create_scene_from_json prefixes resource_root via resolve_path.
        if (const auto scene_rel = wz::json::read_string(proot, "scene")) {
            const wz::fs::Path scene_path =
                wz::fs::join(dir, std::string(*scene_rel));
            const wz::engine::assets::SceneAsset scene =
                ctx_.assets->scenes().create_scene_from_json(
                    wz::engine::assets::SceneFromJSONDesc{ .path = scene_path });
            if (!scene.valid()) {
                ctx_.logger.error("load_project: scene registration failed");
                return false;
            }

            // create_scene_from_json only REGISTERS the scene + its deps (staged
            // after the graph commit); commit + resolve to actually compile it.
            sys.commit();
            std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>>
                scene_errors;
            sys.resolve_all(&scene_errors);

            const wz::engine::assets::SceneHandle scene_handle =
                ctx_.assets->scenes().get_scene(scene);
            const wz::engine::assets::SceneAssetData* scene_data =
                ctx_.assets->scenes().get_scene_data(scene_handle);
            if (!scene_data || !scene_errors.empty()) {
                ctx_.logger.error(
                    "load_project: scene resolve FAILED (errors="
                    + std::to_string(scene_errors.size()) + ")");
                return false;
            }
            // bind_asset_graph already ran above, but scene_nodes_ was empty
            // then; now the scene is loaded, so bridge its renderables to the
            // committed graph keys.
            scene_nodes_ = scene_data->nodes;
            const uint32_t bridged = bridge_scene_renderables(graph_draft_);
            ctx_.logger.info(
                "load_project: scene resolved (nodes="
                + std::to_string(scene_data->nodes.size())
                + ", renderables bridged=" + std::to_string(bridged) + ")");
        }

        return graph_ok;
    }

    void WozzitsApp_v1::simulation_tick()
    {
        renderer_.simulation_tick();
    }

    bool WozzitsApp_v1::render_scene()
    {
        if (!ctx_.assets) {
            return true;
        }
        return renderer_.render_scene(
            scene_nodes_, *ctx_.assets, default_view_projection());
    }

    std::size_t WozzitsApp_v1::resident_gpu_resource_count() const
    {
        return renderer_.resident_gpu_resource_count();
    }

    std::size_t WozzitsApp_v1::resolved_renderable_node_count() const
    {
        std::size_t count = 0;
        for (const wz::engine::assets::SceneNodeAsset& node : scene_nodes_) {
            if (node.renderable_asset) {
                ++count;
            }
        }
        return count;
    }

    std::size_t WozzitsApp_v1::registered_program_count() const
    {
        return renderer_.registered_program_count();
    }

    std::size_t WozzitsApp_v1::registered_shader_count() const
    {
        return renderer_.registered_shader_count();
    }

    std::size_t WozzitsApp_v1::cached_descriptor_table_count() const
    {
        return renderer_.cached_descriptor_table_count();
    }
}
