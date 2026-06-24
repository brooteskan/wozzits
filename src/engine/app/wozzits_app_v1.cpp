// src/engine/app/wozzits_app_v1.cpp

#include <engine/app/wozzits_app_v1.h>

#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_authoring_materialize.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/assets/scene/scene_json_export.h>
#include <engine/assets/scene_asset_module.h>

#include <engine/behavior/behavior_command_apply.h>
#include <engine/behavior/behavior_dispatch.h>
#include <engine/behavior/builtin_behaviors.h>

#include <asset/system.h>

#include <bench/flying_camera.h>
#include <external/json/json_parser.h>
#include <external/json/json_writer.h>
#include <file/filesystem.h>
#include <input/input.h>
#include <math/math_types.h>
#include <math/mat4.h>
#include <math/projection.h>
#include <scene/scene_graph.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace wz::app
{

    WozzitsApp_v1::WozzitsApp_v1(wz::engine::AppContext& ctx)
        : ctx_(ctx)
        , renderer_(*ctx.gpu, ctx.logger)
    {
        // Register the engine's built-in behavior modules up front (mirrors
        // game_app::init). Project DLLs are loaded later, in load_scene, from the
        // manifest's behavior_module_folder.
        wz::engine::behavior::register_builtin_behaviors(
            registry_, plugins_, ctx_.logger);
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
        scene_source_path_ = desc.scene;
        scene_dirty_ = false;
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

        // Load the project's behavior-module DLLs (if any) and register their
        // modules, then materialize the scene's behavior runtime. Mirrors
        // game_app's load -> register sequence; here the modules come from the
        // project manifest's behavior_module_folder rather than only built-ins.
        load_behavior_modules(desc.behavior_module_folder);
        rebuild_behavior_scene();

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

        // Run the scene's behaviors before render prep so this frame draws the
        // post-behavior transforms (game_app dispatches behaviors then applies
        // their command buffer ahead of build_render_*). No-op without a live
        // behavior scene.
        dispatch_scene_behaviors(input, dt);

        renderer_.simulation_tick();
    }

    void WozzitsApp_v1::load_behavior_modules(
        const wz::fs::Path& module_folder)
    {
        if (module_folder.empty()) {
            return;
        }

        // Resolve relative to the asset resource root, matching how the rest of
        // load_scene resolves authored paths through the file system.
        const wz::fs::Path resolved =
            ctx_.assets
                ? ctx_.assets->files().resolve_path(module_folder)
                : module_folder;

        const uint32_t loaded =
            plugins_.load_dynamic_modules_from_directory(
                registry_,
                std::filesystem::path{ resolved },
                &ctx_.logger);
        ctx_.logger.info(
            "load_scene: loaded " + std::to_string(loaded)
            + " behavior module DLL(s) from " + resolved);
    }

    void WozzitsApp_v1::rebuild_behavior_scene()
    {
        behavior_scene_.reset();

        // Nothing to run unless at least one node carries a behavior binding.
        const bool has_behaviors = std::any_of(
            scene_nodes_.begin(),
            scene_nodes_.end(),
            [](const wz::engine::assets::SceneNodeAsset& node) {
                return node.behavior.has_value() || !node.behaviors.empty();
            });
        if (!has_behaviors) {
            return;
        }

        // Materialize a runtime SceneInstance from the authored scene. The
        // renderable resolver is intentionally null: WozzitsApp_v1 renders from
        // scene_nodes_, so the instance is used only for its polytree + behavior
        // tables (renderables in the instance are unused).
        wz::engine::assets::SceneAssetData scene_data;
        scene_data.nodes = scene_nodes_;

        wz::engine::assets::SceneInstantiateContext instantiate_ctx{
            .logger = &ctx_.logger,
            .log_owner = "WozzitsApp_v1",
        };
        wz::engine::assets::SceneInstantiateResult instantiated =
            wz::engine::assets::instantiate_scene(scene_data, instantiate_ctx);
        if (!instantiated.ok()) {
            ctx_.logger.error(
                "behavior scene instantiate failed: "
                + instantiated.error_detail);
            return;
        }

        behavior_scene_ = std::move(instantiated.instance);

        // Initialize behaviors (init callbacks + per-binding/shared state) once
        // for the materialized scene, exactly as game_app does after building
        // its scene.
        wz::engine::behavior::initialize_behaviors(
            *behavior_scene_, registry_, &ctx_.logger);
        ctx_.logger.info(
            "behavior scene initialized (bindings="
            + std::to_string(behavior_scene_->behaviors.size()) + ")");
    }

    void WozzitsApp_v1::dispatch_scene_behaviors(
        const wz::input::InputState& input, float dt)
    {
        if (!behavior_scene_ || behavior_scene_->behaviors.empty()) {
            return;
        }

        // World transforms must be current before dispatch: command application
        // (set_world_translation, motion integration) reads parent world
        // matrices, and behavior transform queries read self/other world. In
        // game_app this is the compile_scene job; here we propagate directly.
        wz::scene::propagate_all(behavior_scene_->storage.polytree);

        // Build a minimal FrameContext carrying time + input. WozzitsApp_v1 has
        // no collision/proximity/input-event subsystems wired (see report), so
        // only frame.update and the held-input snapshot are populated. The empty
        // frame_storage_ collision/input-event tables make those dispatch passes
        // no-ops rather than fabricated events.
        wz::engine::FrameContext frame_context{};
        frame_context.input = input;
        frame_context.frame.interval.start = 0;
        frame_context.frame.interval.end = static_cast<wz::time::Tick>(
            static_cast<double>(dt)
            * static_cast<double>(wz::time::TimeSource::ticks_per_second()));
        frame_context.frame.index = behavior_frame_index_++;

        frame_storage_.behavior_commands.clear();

        // Per-frame deferred-authoring sink: behaviors queue cheap live
        // scene-ECS authoring edits (spawn-child) here mid-dispatch; they are
        // applied below at the frame boundary, AFTER the dispatch loop finishes
        // iterating the scene (so the apply is not reentrant). The buffer is
        // function-local — runtime-owned, per-frame, never crossing a thread or
        // surviving past this tick — which is exactly the standalone-app
        // semantics #204 requires (no EditorRuntimeControl involved).
        wz::engine::behavior::BehaviorAuthoringBuffer authoring;

        wz::engine::behavior::BehaviorFrameContext behavior_ctx{
            .frame_context = &frame_context,
            .frame_storage = &frame_storage_,
            .scene = &*behavior_scene_,
            .behavior_state = &behavior_scene_->behavior_state,
            .commands = &frame_storage_.behavior_commands,
            .gpu_compute = nullptr,
            .authoring = &authoring,
            .logger = &ctx_.logger,
        };
        wz::engine::behavior::dispatch_behaviors(
            *behavior_scene_, registry_, behavior_ctx);

        // Apply the produced command buffer + integrate motion, exactly as
        // game_app's apply_behavior_commands job: transform/velocity commands
        // mutate the instance polytree, then world Y etc. settle on the next
        // propagate.
        std::vector<wz::scene::RuntimeEntityId> changed_entities;
        (void)wz::engine::behavior::apply_behavior_commands(
            *behavior_scene_,
            frame_storage_.behavior_commands.commands,
            &changed_entities);

        std::vector<wz::scene::RuntimeEntityId> velocity_changed;
        (void)wz::engine::behavior::integrate_motion(
            *behavior_scene_, dt, &velocity_changed);
        changed_entities.insert(
            changed_entities.end(),
            velocity_changed.begin(),
            velocity_changed.end());
        std::sort(changed_entities.begin(), changed_entities.end());
        changed_entities.erase(
            std::unique(changed_entities.begin(), changed_entities.end()),
            changed_entities.end());

        if (!changed_entities.empty()) {
            // Re-propagate so world matrices (and any next-frame world-space
            // reads) reflect the applied local changes.
            wz::scene::propagate_all(behavior_scene_->storage.polytree);

            // Write the changed local transforms back into scene_nodes_ so the
            // next render_scene() (which draws from scene_nodes_, not the
            // instance) shows the behavior result. Decompose each changed
            // node's instance-local matrix to TRS and overwrite the matching
            // authored node's transform.
            for (const wz::scene::RuntimeEntityId entity : changed_entities) {
                if (entity >= behavior_scene_->runtime_to_authored.size()) {
                    continue;
                }
                const wz::math::Mat4& local = wz::core::graph::node_data(
                    behavior_scene_->storage.polytree, entity).local;

                wz::math::Transform trs{};
                if (!wz::math::decompose_trs(local, trs)) {
                    continue;
                }

                wz::engine::assets::AuthoredTransform authored{};
                authored.translation[0] = trs.position.x;
                authored.translation[1] = trs.position.y;
                authored.translation[2] = trs.position.z;
                authored.rotation_quat[0] = trs.rotation.x;
                authored.rotation_quat[1] = trs.rotation.y;
                authored.rotation_quat[2] = trs.rotation.z;
                authored.rotation_quat[3] = trs.rotation.w;
                authored.scale[0] = trs.scale.x;
                authored.scale[1] = trs.scale.y;
                authored.scale[2] = trs.scale.z;

                wz::engine::assets::SceneNodeAsset* node =
                    wz::engine::assets::find_scene_node(
                        scene_nodes_,
                        behavior_scene_->runtime_to_authored[entity]);
                if (node) {
                    node->local = authored;
                }
            }
        }

        // Frame-boundary drain of behavior-issued deferred authoring (#204).
        // This runs AFTER the dispatch loop has finished iterating the scene, so
        // mutating it here is not reentrant. Each request goes through the SAME
        // apply method the host's add_child uses (add_child_node) — the single
        // converged apply path. add_child_node re-materializes the behavior
        // runtime (rebuild_behavior_scene) on success, which is why this is the
        // LAST thing the tick does: every read of behavior_scene_ above has
        // already happened, and behavior_scene_ may be rebuilt out from under us
        // here safely. Fire-and-forget: no id flows back to the behavior. The
        // parents were resolved to authored ids at enqueue time, so they remain
        // valid even as a prior add in this same drain renumbers runtime ids.
        for (const wz::scene::AuthoredEntityId& parent :
             authoring.spawn_child_parents)
        {
            const wz::engine::assets::SceneAddChildResult result =
                add_child_node(parent);
            if (!result.ok) {
                ctx_.logger.warn(
                    "behavior spawn_child rejected for parent '" + parent
                    + "': " + result.error);
            }
        }

        // remove_node drain: same frame-boundary, same single converged apply
        // path the host's remove uses (remove_node, which also rebuilds the
        // behavior runtime on success). Authored-id targets stay valid even as a
        // prior add/remove in this same drain renumbers runtime ids.
        for (const wz::scene::AuthoredEntityId& target :
             authoring.remove_node_targets)
        {
            if (!remove_node(target)) {
                ctx_.logger.warn(
                    "behavior remove_node rejected for node '" + target + "'");
            }
        }

        // set_renderable_asset drain: same frame-boundary, same apply method the
        // host uses (set_node_renderable_asset). This is a cheap field write +
        // dirty flag (no behavior-runtime rebuild, no asset-DAG recompile), so it
        // is order-independent of the structural drains above.
        for (const wz::engine::behavior::BehaviorSetRenderableRequest& request :
             authoring.set_renderable_requests)
        {
            if (!set_node_renderable_asset(
                    request.node_id, request.asset_graph_node_id)) {
                ctx_.logger.warn(
                    "behavior set_renderable_asset rejected for node '"
                    + request.node_id + "'");
            }
        }
    }

    std::size_t WozzitsApp_v1::active_behavior_binding_count() const
    {
        return behavior_scene_ ? behavior_scene_->behaviors.size() : 0u;
    }

    std::optional<wz::math::Vec3> WozzitsApp_v1::node_local_translation(
        const wz::scene::AuthoredEntityId& id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, id);
        if (!node) {
            return std::nullopt;
        }
        return wz::math::Vec3{
            node->local.translation[0],
            node->local.translation[1],
            node->local.translation[2],
        };
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
        scene_dirty_ = true;
        return true;
    }

    wz::engine::assets::SceneAddChildResult WozzitsApp_v1::add_child_node(
        const wz::scene::AuthoredEntityId& parent_id)
    {
        wz::engine::assets::SceneAddChildResult result =
            wz::engine::assets::add_child_scene_node(scene_nodes_, parent_id);
        scene_dirty_ = scene_dirty_ || result.ok;
        // A structural change invalidates the behavior runtime's entity ids; if
        // behaviors are live, re-materialize so their runtime tracks the edit.
        if (result.ok && behavior_scene_) {
            rebuild_behavior_scene();
        }
        return result;
    }

    bool WozzitsApp_v1::set_node_properties(
        const wz::scene::AuthoredEntityId& id,
        std::string name,
        bool visible)
    {
        const bool ok = wz::engine::assets::set_scene_node_properties(
            scene_nodes_, id, std::move(name), visible);
        scene_dirty_ = scene_dirty_ || ok;
        return ok;
    }

    bool WozzitsApp_v1::reparent_node(
        const wz::scene::AuthoredEntityId& id,
        const wz::scene::AuthoredEntityId& new_parent_id)
    {
        const bool ok = wz::engine::assets::reparent_scene_node(
            scene_nodes_, id, new_parent_id);
        scene_dirty_ = scene_dirty_ || ok;
        if (ok && behavior_scene_) {
            rebuild_behavior_scene();
        }
        return ok;
    }

    bool WozzitsApp_v1::remove_node(const wz::scene::AuthoredEntityId& id)
    {
        const bool removed =
            !wz::engine::assets::remove_scene_node(scene_nodes_, id).empty();
        scene_dirty_ = scene_dirty_ || removed;
        if (removed && behavior_scene_) {
            rebuild_behavior_scene();
        }
        return removed;
    }

    // ─── Live behavior-binding authoring ────────────────────────────────────
    // Each applies the matching scene_asset_data.h helper to scene_nodes_, then
    // (on success) marks the scene dirty and re-materializes the behavior
    // runtime so the change takes effect. The rebuild is UNCONDITIONAL on
    // success — unlike the structural edits above, which rebuild only when a
    // behavior scene already exists — because adding the first binding to a node
    // that had none must create the behavior runtime where there was none.

    wz::engine::assets::SceneAddBehaviorResult WozzitsApp_v1::add_node_behavior(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& module)
    {
        wz::engine::assets::SceneAddBehaviorResult result =
            wz::engine::assets::add_node_behavior(scene_nodes_, node_id, module);
        if (result.ok) {
            scene_dirty_ = true;
            rebuild_behavior_scene();
        }
        return result;
    }

    bool WozzitsApp_v1::remove_node_behavior(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id)
    {
        const bool ok = wz::engine::assets::remove_node_behavior(
            scene_nodes_, node_id, binding_id);
        if (ok) {
            scene_dirty_ = true;
            rebuild_behavior_scene();
        }
        return ok;
    }

    bool WozzitsApp_v1::set_node_behavior_enabled(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        bool enabled)
    {
        const bool ok = wz::engine::assets::set_node_behavior_enabled(
            scene_nodes_, node_id, binding_id, enabled);
        if (ok) {
            scene_dirty_ = true;
            rebuild_behavior_scene();
        }
        return ok;
    }

    bool WozzitsApp_v1::set_node_behavior_fields(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const std::string& label,
        const std::string& module)
    {
        const bool ok = wz::engine::assets::set_node_behavior_fields(
            scene_nodes_, node_id, binding_id, label, module);
        if (ok) {
            scene_dirty_ = true;
            rebuild_behavior_scene();
        }
        return ok;
    }

    bool WozzitsApp_v1::set_node_behavior_events(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const std::vector<std::string>& events)
    {
        const bool ok = wz::engine::assets::set_node_behavior_events(
            scene_nodes_, node_id, binding_id, events);
        if (ok) {
            scene_dirty_ = true;
            rebuild_behavior_scene();
        }
        return ok;
    }

    bool WozzitsApp_v1::set_node_behavior_config(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const wz::engine::assets::SceneBehaviorConfigValue& value)
    {
        const bool ok = wz::engine::assets::set_node_behavior_config(
            scene_nodes_, node_id, binding_id, value);
        if (ok) {
            scene_dirty_ = true;
            rebuild_behavior_scene();
        }
        return ok;
    }

    bool WozzitsApp_v1::clear_node_behavior_config(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const std::string& key)
    {
        const bool ok = wz::engine::assets::clear_node_behavior_config(
            scene_nodes_, node_id, binding_id, key);
        if (ok) {
            scene_dirty_ = true;
            rebuild_behavior_scene();
        }
        return ok;
    }

    // ─── Live optional-component authoring ──────────────────────────────────
    // Add/remove one of the five editor-managed optional components on a node in
    // scene_nodes_, then (on success) mark the scene dirty. Unlike the behavior
    // verbs above, these do NOT rebuild_behavior_scene(): none of camera /
    // renderable / proximity / collision / motion creates a behavior binding, so
    // the behavior runtime is unaffected. The renderer reads scene_nodes_ fresh
    // each frame, so the next render reflects the change. An unknown kind (or
    // missing node) is a logged no-op (fail closed).

    bool WozzitsApp_v1::add_node_component(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& kind)
    {
        const bool ok = wz::engine::assets::add_node_optional_component(
            scene_nodes_, node_id, kind);
        if (ok) {
            scene_dirty_ = true;
        }
        else {
            ctx_.logger.warn(
                "add_node_component: no-op (node '" + node_id
                + "' missing or unknown component kind '" + kind + "')");
        }
        return ok;
    }

    bool WozzitsApp_v1::remove_node_component(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& kind)
    {
        const bool ok = wz::engine::assets::remove_node_optional_component(
            scene_nodes_, node_id, kind);
        if (ok) {
            scene_dirty_ = true;
        }
        else {
            ctx_.logger.warn(
                "remove_node_component: no-op (node '" + node_id
                + "' missing or unknown component kind '" + kind + "')");
        }
        return ok;
    }

    bool WozzitsApp_v1::set_node_renderable_asset(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        const bool ok = wz::engine::assets::set_node_renderable_asset(
            scene_nodes_, node_id, asset_graph_node_id);
        if (ok) {
            scene_dirty_ = true;
        }
        else {
            ctx_.logger.warn(
                "set_node_renderable_asset: no-op (node '" + node_id
                + "' missing)");
        }
        return ok;
    }

    bool WozzitsApp_v1::node_has_component(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& kind) const
    {
        return wz::engine::assets::node_has_optional_component(
            scene_nodes_, node_id, kind);
    }

    std::size_t WozzitsApp_v1::child_node_count(
        const wz::scene::AuthoredEntityId& parent_id) const
    {
        std::size_t count = 0;
        for (const wz::engine::assets::SceneNodeAsset& node : scene_nodes_) {
            if (node.parent_id && *node.parent_id == parent_id) {
                ++count;
            }
        }
        return count;
    }

    std::optional<wz::asset::AssetGraphDraftNodeId>
    WozzitsApp_v1::node_renderable_asset_node_id(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node) {
            return std::nullopt;
        }
        return node->renderable_asset_node_id;
    }

    bool WozzitsApp_v1::save_scene()
    {
        if (!scene_dirty_) {
            return true;  // nothing changed since load / last save
        }
        if (scene_source_path_.empty()) {
            return false;
        }

        const wz::fs::Path resource_root =
            ctx_.assets ? ctx_.assets->resource_root() : wz::fs::Path{};
        const wz::fs::Path path =
            wz::fs::is_absolute(scene_source_path_) || resource_root.empty()
                ? scene_source_path_
                : wz::fs::join(resource_root, scene_source_path_);

        // Read the existing scene so its non-node data (lights, defaults, sky)
        // is preserved; only the nodes array is replaced from the live edits.
        wz::json::JSONDocument document;
        {
            std::ifstream file(path, std::ios::binary);
            if (file) {
                const std::string text(
                    (std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
                wz::json::JSONParseResult parsed =
                    wz::json::parse_json_string(text);
                if (parsed.ok && parsed.document.root) {
                    document = std::move(parsed.document);
                }
            }
        }
        if (document.root) {
            wz::engine::assets::set_scene_document_nodes(document, scene_nodes_);
        }
        else {
            // No readable source — emit a fresh scene document from the nodes.
            wz::engine::assets::SceneAssetData snapshot;
            snapshot.nodes = scene_nodes_;
            document =
                wz::engine::assets::export_scene_to_json_document(snapshot);
        }

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return false;
        }
        out << wz::json::serialize_json(document);
        if (!out.good()) {
            return false;
        }

        scene_dirty_ = false;
        ctx_.logger.info("save_scene: scene persisted");
        return true;
    }

    bool WozzitsApp_v1::render_scene()
    {
        if (!ctx_.assets) {
            return true;
        }
        // The clipmap landscape snaps its lattice to the camera world position.
        // Use the free-fly camera's position; it matches compute_view_projection
        // on the app-camera path. (When an editor camera override is active the
        // view is driven externally and a matching override position is a future
        // editor concern — see the camera TODO in the header.)
        const wz::math::Vec3 camera_world_pos{
            camera_.x, camera_.y, camera_.z };
        return renderer_.render_scene(
            scene_nodes_, *ctx_.assets, compute_view_projection(),
            camera_world_pos);
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
