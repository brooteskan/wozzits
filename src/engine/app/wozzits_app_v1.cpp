// src/engine/app/wozzits_app_v1.cpp

#include <engine/app/wozzits_app_v1.h>

#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_authoring_materialize.h>
#include <engine/assets/scene_asset_module.h>

#include <asset/system.h>

#include <bench/flying_camera.h>
#include <external/json/json_parser.h>
#include <file/filesystem.h>
#include <input/input.h>
#include <math/math_types.h>
#include <math/mat4.h>
#include <math/projection.h>

#include <chrono>
#include <string>
#include <utility>

namespace wz::app
{

    WozzitsApp_v1::WozzitsApp_v1(wz::engine::AppContext& ctx)
        : ctx_(ctx)
        , renderer_(*ctx.gpu, ctx.logger)
    {
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

        // Register the draft as the asset set: materialize keys -> registrations
        // -> wholesale replace, then normalize the committed draft (compact, mark
        // nodes Existing, rebuild indexes). This is the single-sourced engine
        // pipeline; the app no longer hand-rolls it. NOTE: "commit" here means
        // "commit the draft as the registered set" — distinct from sys.commit()
        // (the DAG rebuild) below. Writes validation messages into the draft.
        const auto commit = ctx_.assets->commit_asset_graph_draft(draft);
        using CommitStatus = wz::engine::assets::EngineAssetLibrary::
            AssetGraphDraftCommitReport::Status;
        if (commit.status != CommitStatus::Success) {
            result.ok = false;
            result.diagnostics = draft.validation_messages;
            if (commit.status == CommitStatus::ReplaceFailed) {
                result.diagnostics.push_back(
                    wz::asset::AssetGraphDraftValidationMessage{
                        .severity =
                            wz::asset::AssetGraphDraftValidationSeverity::Error,
                        .message = "asset graph registration rejected",
                    });
            }
            ctx_.logger.error(
                std::string("bind_asset_graph: draft commit rejected (")
                + (commit.status == CommitStatus::MaterializeFailed
                       ? "materialize"
                       : "replace")
                + ") diagnostics=" + std::to_string(result.diagnostics.size()));
            return result;
        }

        // Rebuild the DAG with the newly registered set (commit() only
        // rebuilds the graph; it does NOT run compilers). Through the library so
        // the app drives the engine API, not the raw AssetSystem.
        if (!ctx_.assets->commit()) {
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
        // Through the library so we get its resolve logging + a structured report.
        const wz::engine::assets::ResolveReport resolve_report =
            ctx_.assets->resolve_all();
        if (!resolve_report.ok()) {
            result.ok = false;
            for (const wz::engine::assets::ResolveFailure& failure :
                 resolve_report.failures)
            {
                const auto node = draft.node_by_key.find(failure.key);
                draft.validation_messages.push_back(
                    wz::asset::AssetGraphDraftValidationMessage{
                        .severity =
                            wz::asset::AssetGraphDraftValidationSeverity::Error,
                        .node =
                            node == draft.node_by_key.end()
                                ? wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
                                : node->second,
                        .message = "asset resolve failed (ResolveError "
                            + std::to_string(
                                static_cast<int>(failure.error)) + ")",
                    });
            }
            result.diagnostics = draft.validation_messages;
            ctx_.logger.error(
                "bind_asset_graph: resolve failed resolved="
                + std::to_string(resolve_report.resolved_count)
                + " failures="
                + std::to_string(resolve_report.failures.size()));
            return result;
        }

        graph_epoch_ = sys.registration_epoch();

        // Reconcile the shared rhi program/shader registries AFTER resolve: keep
        // the entries whose AssetKey is still in the live registered set, release
        // the rest. Survivor-preserving (not a wholesale clear), so a same-content
        // rebind — where resolve is a cache hit and the compiler is skipped —
        // keeps the already-registered program/shaders instead of emptying the
        // registry (which, once the render-program compiler's render-time
        // D3DCompile fallback is gone, would fail to realize). Content-addressed
        // names make it exact: unchanged content keeps its slot; changed content
        // adds a new entry and this releases the stale one (bounded across swaps).
        ctx_.assets->reconcile_rhi_render_program_registries();

        // Deferred-release shared-registry residency for assets that dropped out
        // of the freshly committed graph. Asset-type agnostic — the library owns
        // the tracking; the app no longer knows about any specific asset's GPU
        // buffers. This must run BEFORE on_graph_changed()'s collect: it uses
        // release() only, and on_graph_changed()'s wait_idle-guarded
        // collect(UINT64_MAX) reclaims both these and the renderer-side buffers.
        ctx_.assets->release_unregistered_rhi_resources();

        // Rebind the renderer to the new graph: invalidate the realized caches
        // (keyed by the OUTGOING graph's AssetKeys) and deferred-release the
        // outgoing graph's GPU resources, so renderables re-realize against the
        // freshly committed keys instead of drawing the previous graph.
        renderer_.on_graph_changed();

        // The swap minted new AssetKeys; re-point the scene's renderables at
        // them. (On the first bind during load_scene the scene is not loaded
        // yet, so this is a no-op there and load_scene re-runs it after.)
        const uint32_t bridged =
            wz::engine::assets::bridge_scene_renderable_keys(scene_nodes_, draft);

        result.ok = true;
        result.diagnostics = draft.validation_messages;
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count();
        ctx_.logger.info(
            "bind_asset_graph: compile succeeded registered="
            + std::to_string(sys.registered_assets().size())
            + " resolved=" + std::to_string(resolve_report.resolved_count)
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

        // Compile the asset graph (the unproven path): read the v2 graph JSON
        // -> draft (shared engine loader) -> bind (swap + resolve).
        const wz::fs::FileResult<std::string> graph_text =
            wz::fs::read_file_text(
                ctx_.assets->files().resolve_path(desc.asset_graph));
        if (!graph_text) {
            ctx_.logger.error(
                "load_scene: cannot read asset graph: " + desc.asset_graph);
            return false;
        }
        const wz::json::JSONParseResult gj =
            wz::json::parse_json_string(graph_text.value);
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
        // Through the library, matching bind_asset_graph.
        ctx_.assets->commit();
        const wz::engine::assets::ResolveReport scene_resolve =
            ctx_.assets->resolve_all();

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
        const uint32_t bridged =
            wz::engine::assets::bridge_scene_renderable_keys(
                scene_nodes_, graph_draft_);
        if (!scene_resolve.ok()) {
            ctx_.logger.warn(
                "load_scene: scene resolved with errors="
                + std::to_string(scene_resolve.failures.size())
                + " (loaded anyway for editor recovery)");
        }
        ctx_.logger.info(
            "load_scene: scene resolved (nodes="
            + std::to_string(scene_data->nodes.size())
            + ", renderables bridged=" + std::to_string(bridged) + ")");

        return graph_ok && scene_resolve.ok();
    }

    void WozzitsApp_v1::simulation_tick(
        const wz::input::InputState& input, float dt)
    {
        wz::bench::update_flying_camera(camera_, input, dt);
        if (input.window.width > 0 && input.window.height > 0) {
            aspect_ = static_cast<float>(input.window.width)
                / static_cast<float>(input.window.height);
        }
        renderer_.simulation_tick();
    }

    bool WozzitsApp_v1::set_node_transform(
        const wz::scene::AuthoredEntityId& id,
        const wz::engine::assets::AuthoredTransform& transform)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, id);
        if (!node) {
            return false;
        }
        wz::engine::assets::set_transform(*node, transform);
        return true;
    }

    bool WozzitsApp_v1::render_scene()
    {
        if (!ctx_.assets) {
            return true;
        }
        return renderer_.render_scene(
            scene_nodes_, *ctx_.assets, compute_view_projection());
    }

    void WozzitsApp_v1::set_camera_override(
        const wz::math::Mat4& view_projection)
    {
        camera_override_ = view_projection;
    }

    void WozzitsApp_v1::clear_camera_override()
    {
        camera_override_.reset();
    }

    wz::math::Mat4 WozzitsApp_v1::compute_view_projection() const
    {
        if (camera_override_) {
            return *camera_override_;
        }

        // Free-fly camera -> left-handed DX view-projection (the renderer's
        // convention). aspect tracks the window from the latest input.
        const wz::math::Mat4 view = wz::bench::view_matrix(camera_);
        const wz::math::Mat4 proj = wz::math::projection_perspective_dx(
            camera_fov_y_, aspect_, camera_near_, camera_far_);
        return wz::math::mul(proj, view);
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

    std::size_t WozzitsApp_v1::render_time_program_bridge_count() const
    {
        return renderer_.render_time_program_bridge_count();
    }
}
