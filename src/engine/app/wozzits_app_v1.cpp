// src/engine/app/wozzits_app_v1.cpp

#include <engine/app/wozzits_app_v1.h>

#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene_asset_module.h>

#include <asset/system.h>

#include <external/json/json_parser.h>
#include <file/filesystem.h>
#include <math/math_types.h>
#include <math/mat4.h>

#include <cmath>
#include <chrono>
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
        const auto started = std::chrono::steady_clock::now();
        AssetGraphCompileResult result;
        ctx_.logger.info(
            "bind_asset_graph: compile requested nodes="
            + std::to_string(draft.nodes.size())
            + " edges=" + std::to_string(draft.edges.size()));

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
            ctx_.logger.error(
                "bind_asset_graph: materialization failed diagnostics="
                + std::to_string(result.diagnostics.size()));
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
            ctx_.logger.error("bind_asset_graph: registration replace failed");
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
            ctx_.logger.error("bind_asset_graph: DAG rebuild failed");
            return result;
        }

        // Resolve: the actual compile pass — run every node's compiler, filling
        // ResourceHandles. Failures (missing files, compiler errors) surface here.
        std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>>
            resolve_errors;
        const uint32_t resolved_count = sys.resolve_all(&resolve_errors);
        if (!resolve_errors.empty()) {
            result.ok = false;
            for (const auto& [key, error] : resolve_errors) {
                const auto node = draft.node_by_key.find(key);
                draft.validation_messages.push_back(
                    wz::asset::AssetGraphDraftValidationMessage{
                        .severity =
                            wz::asset::AssetGraphDraftValidationSeverity::Error,
                        .node =
                            node == draft.node_by_key.end()
                                ? wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
                                : node->second,
                        .message = "asset resolve failed (ResolveError "
                            + std::to_string(static_cast<int>(error)) + ")",
                    });
            }
            result.diagnostics = draft.validation_messages;
            ctx_.logger.error(
                "bind_asset_graph: resolve failed resolved="
                + std::to_string(resolved_count)
                + " failures=" + std::to_string(resolve_errors.size()));
            return result;
        }

        graph_epoch_ = sys.registration_epoch();

        // Rebind the renderer to the new graph: invalidate the realized caches
        // (keyed by the OUTGOING graph's AssetKeys) and deferred-release the
        // outgoing graph's GPU resources, so renderables re-realize against the
        // freshly committed keys instead of drawing the previous graph.
        renderer_.on_graph_changed();

        // The swap minted new AssetKeys; re-point the scene's renderables at
        // them. (On the first bind during load_scene the scene is not loaded
        // yet, so this is a no-op there and load_scene re-runs it after.)
        const uint32_t bridged = bridge_scene_renderables(draft);

        result.ok = true;
        result.diagnostics = draft.validation_messages;
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count();
        ctx_.logger.info(
            "bind_asset_graph: compile succeeded registered="
            + std::to_string(sys.registered_assets().size())
            + " resolved=" + std::to_string(resolved_count)
            + " renderables_bridged=" + std::to_string(bridged)
            + " diagnostics=" + std::to_string(result.diagnostics.size())
            + " ms=" + std::to_string(elapsed_ms));
        return result;
    }

    bool WozzitsApp_v1::adopt_asset_graph(wz::asset::AssetGraph&& /*resolved*/)
    {
        // TODO(next): adopt an already-resolved DAG (async/baked handoff). Needs
        // an AssetSystem entry to install a pre-resolved graph; not yet wired.
        ctx_.logger.warn("adopt_asset_graph: not yet implemented");
        return false;
    }

    bool WozzitsApp_v1::load_scene(const WozzitsAppSceneLoadDesc& desc)
    {
        if (!ctx_.assets) {
            ctx_.logger.error("load_scene: no asset library");
            return false;
        }
        if (desc.asset_graph.empty()) {
            ctx_.logger.error("load_scene: asset graph path is empty");
            return false;
        }
        if (desc.scene.empty()) {
            ctx_.logger.error("load_scene: scene path is empty");
            return false;
        }
        wz::asset::AssetSystem& sys = ctx_.assets->system();

        // Compile the asset graph (the unproven path): read the v2 graph JSON
        // -> draft (shared engine loader) -> bind (swap + resolve).
        const std::string graph_text =
            read_text_file(ctx_.assets->files().resolve_path(desc.asset_graph));
        if (graph_text.empty()) {
            ctx_.logger.error(
                "load_scene: cannot read asset graph: " + desc.asset_graph);
            return false;
        }
        const wz::json::JSONParseResult gj =
            wz::json::parse_json_string(graph_text);
        if (!gj.ok || !gj.document.root) {
            ctx_.logger.error("load_scene: invalid asset graph json");
            return false;
        }

        graph_draft_ = wz::asset::AssetGraphDraft{};
        std::string error;
        if (!wz::engine::assets::load_asset_graph_draft_from_v2_json(
                *gj.document.root, graph_draft_, error))
        {
            ctx_.logger.error(
                "load_scene: asset graph parse failed: " + error);
            return false;
        }

        const AssetGraphCompileResult bound = bind_asset_graph(graph_draft_);
        const bool graph_ok = bound.ok;
        ctx_.logger.info(
            "load_scene: asset graph "
            + std::string(bound.ok ? "compiled" : "FAILED")
            + " (registered="
            + std::to_string(ctx_.assets->system().registered_assets().size())
            + ", diagnostics=" + std::to_string(bound.diagnostics.size())
            + ")");
        for (const auto& d : bound.diagnostics) {
            if (d.severity
                == wz::asset::AssetGraphDraftValidationSeverity::Error)
            {
                ctx_.logger.error("load_scene:   " + d.message);
            }
        }
        // Note: do NOT bail when the graph bind reported errors. Load the scene
        // anyway so scene_nodes_ is populated; the user can then fix the graph in
        // the editor and a later successful rebind will render. Bailing here left
        // scene_nodes_ empty, so even a subsequent good compile drew nothing.

        const wz::engine::assets::SceneAsset scene =
            ctx_.assets->scenes().create_scene_from_json(
                wz::engine::assets::SceneFromJSONDesc{ .path = desc.scene });
        if (!scene.valid()) {
            ctx_.logger.error("load_scene: scene registration failed");
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
        if (!scene_data) {
            ctx_.logger.error("load_scene: scene resolve FAILED (no scene data)");
            return false;
        }

        // bind_asset_graph already ran above, but scene_nodes_ was empty then;
        // now the scene is loaded, so bridge its renderables to the committed
        // graph keys. Populate scene_nodes_ even with graph/scene compile errors
        // so a later good rebind can render.
        scene_nodes_ = scene_data->nodes;
        const uint32_t bridged = bridge_scene_renderables(graph_draft_);
        if (!scene_errors.empty()) {
            ctx_.logger.warn(
                "load_scene: scene resolved with errors="
                + std::to_string(scene_errors.size())
                + " (loaded anyway for editor recovery)");
        }
        ctx_.logger.info(
            "load_scene: scene resolved (nodes="
            + std::to_string(scene_data->nodes.size())
            + ", renderables bridged=" + std::to_string(bridged) + ")");

        return graph_ok && scene_errors.empty();
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
