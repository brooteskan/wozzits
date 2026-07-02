// src/engine/app/wozzits_app_v1.cpp

#include <engine/app/wozzits_app_v1.h>

#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_authoring_materialize.h>
#include <engine/assets/scene/scene_compilers.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/assets/scene/scene_json_export.h>
#include <engine/assets/scene/prefab_instantiate.h>
#include <engine/assets/scene/scene_subtree_export.h>
#include <engine/assets/scene_asset_module.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/type_extensions.h>
#include <engine/audio/scene_audio.h>

#include <engine/behavior/behavior_command_apply.h>
#include <engine/behavior/behavior_dispatch.h>
#include <engine/behavior/builtin_behaviors.h>
#include <engine/behavior/quantum_agent_behaviors.h>
#include <engine/collision/collision_frame.h>
#include <engine/input_events.h>

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
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wz::app
{
    namespace
    {
        // FNV-1a/32 over a prefab name. Matches wz_prefab_hash (the behavior-side
        // constexpr) bit-for-bit so a SPAWN_PREFAB command's hash resolves to the
        // prefab registered under the same name.
        uint32_t prefab_name_hash(std::string_view name) noexcept
        {
            uint32_t h = 2166136261u;
            for (const unsigned char c : name) {
                h ^= static_cast<uint32_t>(c);
                h *= 16777619u;
            }
            return h;
        }

        // Decompose a simulation-node LOCAL matrix into an authored TRS. This is
        // the same lossy Mat4 -> TRS step the old per-frame write-back did; #221
        // moved it off the frame path so it runs only when scene_nodes_ actually
        // needs an authored transform (save + editor read-back). Returns false
        // (leave the authored transform untouched) when decompose_trs rejects the
        // matrix, matching the write-back's skip-on-failure.
        bool authored_transform_from_local(
            const wz::math::Mat4& local,
            wz::engine::assets::AuthoredTransform& out)
        {
            wz::math::Transform trs{};
            if (!wz::math::decompose_trs(local, trs)) {
                return false;
            }
            out.translation[0] = trs.position.x;
            out.translation[1] = trs.position.y;
            out.translation[2] = trs.position.z;
            out.rotation_quat[0] = trs.rotation.x;
            out.rotation_quat[1] = trs.rotation.y;
            out.rotation_quat[2] = trs.rotation.z;
            out.rotation_quat[3] = trs.rotation.w;
            out.scale[0] = trs.scale.x;
            out.scale[1] = trs.scale.y;
            out.scale[2] = trs.scale.z;
            return true;
        }
    }

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

        // Re-register the GLB scene-source descriptors NOW: after the draft's
        // wholesale replace_registered_assets (commit_asset_graph_draft above)
        // but before the DAG rebuild + resolve below, so their assets land in the
        // freshly committed registered set and compile in the SAME resolve pass —
        // present when graft_scene_sources() reads scene_source at the end. This
        // is how they survive the wholesale replace (the GLB scenes are not part
        // of the draft): they are re-registered on every (re)bind. On the first
        // bind during load_scene, scene_nodes_ is still empty, so this is a no-op
        // there; load_scene re-resolves after populating scene_nodes_.
        resolve_glb_scene_sources();

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
                std::string message = "asset resolve failed: "
                    + std::string(
                        wz::asset::resolve_error_name(failure.error));
                if (!failure.detail.empty()) {
                    message += ": " + failure.detail;
                }
                draft.validation_messages.push_back(
                    wz::asset::AssetGraphDraftValidationMessage{
                        .severity =
                            wz::asset::AssetGraphDraftValidationSeverity::Error,
                        .node =
                            node == draft.node_by_key.end()
                                ? wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
                                : node->second,
                        .message = std::move(message),
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
        // Re-assemble geometry+program bindings into renderables on every
        // (re)bind (#213 increment 2): mirrors bridge for the binding path, AFTER
        // it so the binding overrides a pre-built renderable on the same node. The
        // created renderables need their own commit()+resolve_all() (the main
        // resolve above already ran). No-op on the first bind during load_scene
        // (scene_nodes_ empty); load_scene re-assembles after populating nodes.
        const std::size_t render_bindings_assembled =
            assemble_render_bindings(draft);
        if (render_bindings_assembled > 0) {
            ctx_.assets->commit();
            const wz::engine::assets::ResolveReport binding_resolve =
                ctx_.assets->resolve_all();
            if (!binding_resolve.ok()) {
                ctx_.logger.warn(
                    "bind_asset_graph: render-binding asset(s) resolved with "
                    "errors="
                    + std::to_string(binding_resolve.failures.size()));
            }
        }
        // Re-bridge scene-source references + re-graft on every (re)bind so a
        // graph swap's new Scene keys flow through to the grafted children (#213).
        // On the first bind during load_scene the scene is not loaded yet, so
        // this is a no-op there; load_scene re-runs it after populating nodes.
        const uint32_t scene_sources_bridged =
            wz::engine::assets::bridge_scene_source_keys(scene_nodes_, draft);
        (void)scene_sources_bridged;
        // Re-point authored collision references at the freshly committed graph's
        // collision keys (issue #216/#217), mirroring the renderable/source
        // bridges above. No-op on the first bind during load_scene (scene_nodes_
        // empty); load_scene re-runs it after populating nodes.
        const uint32_t collisions_bridged =
            wz::engine::assets::bridge_scene_collision_keys(scene_nodes_, draft);
        (void)collisions_bridged;
        const std::size_t bind_grafted = graft_scene_sources();
        // Assemble the freshly grafted children's intrinsic geometry bindings
        // (#213 increment 3) — the pre-graft assemble above ran before they
        // existed, so the subtree would not draw without this second pass.
        if (bind_grafted > 0) {
            rematerialize_render_bindings();
        }

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

        // Remember the graph + module paths so open_scene can swap just the scene
        // (prefab editing) while reusing this same project asset graph.
        asset_graph_path_ = desc.asset_graph;
        behavior_module_folder_ = desc.behavior_module_folder;
        // The FIRST scene loaded is the project's main scene; keep it so open_scene
        // can return to it (a scenelet swap via open_scene must not clobber this).
        if (main_scene_path_.empty()) {
            main_scene_path_ = desc.scene;
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
        // Snapshot the authored node count now: the GLB scene-source resolve
        // below runs a second commit() + resolve_all() which can invalidate the
        // scene_data pointer (the scene table may move entries), so it must not
        // be dereferenced afterwards (only this count is needed, for logging).
        const std::size_t authored_scene_node_count = scene_data->nodes.size();

        // Materialize the scene's authored DATA components into registered assets
        // (#216): a node may author a terrain/collision/scalar-field SOURCE (e.g.
        // a heightfield terrain with a constrain_movement constraint surface, or
        // an inline procedural scalar field) rather than a pre-resolved asset key.
        // create_scene_from_json only PARSES those sources; it does not build the
        // backing assets — that is the editor's materialize step, which the
        // runtime load path must run too so the per-frame constraint pipeline can
        // resolve a surface. Renderable creation is intentionally OFF here: the
        // RHI render path assembles renderables from graph bindings (above /
        // below), not from these preview/terrain-debug recipes. Data-only, so a
        // scene without any authored sources is a clean no-op.
        {
            wz::engine::assets::SceneAssetData materialize_scene;
            materialize_scene.nodes = scene_nodes_;
            const wz::engine::assets::SceneAuthoringMaterializeReport
                materialize =
                    wz::engine::assets::materialize_scene_authoring_components(
                        materialize_scene,
                        *ctx_.assets,
                        wz::engine::assets::SceneAuthoringMaterializeOptions{
                            .create_preview_renderables = false,
                            .create_terrain_surface_renderables = false,
                            .create_terrain_debug_renderables = false,
                        });
            if (!materialize.ok) {
                ctx_.logger.warn(
                    "load_scene: scene authoring materialize reported error: "
                    + materialize.error + " (loaded anyway for editor recovery)");
            }
            // Adopt the resolved keys the materialize wrote back (terrain_asset,
            // constraint_surface_asset, collision_asset, scalar_field_asset, ...)
            // then compile the freshly registered assets so the instance + the
            // collision frame can resolve them.
            scene_nodes_ = std::move(materialize_scene.nodes);
            ctx_.assets->commit();
            const wz::engine::assets::ResolveReport materialize_resolve =
                ctx_.assets->resolve_all();
            if (!materialize_resolve.ok()) {
                ctx_.logger.warn(
                    "load_scene: materialized authoring asset(s) resolved with "
                    "errors=" + std::to_string(materialize_resolve.failures.size())
                    + " (loaded anyway for editor recovery)");
            }
        }
        scene_source_path_ = desc.scene;
        scene_dirty_ = false;
        grafted_node_ids_.clear();
        const uint32_t bridged =
            wz::engine::assets::bridge_scene_renderable_keys(
                scene_nodes_, graph_draft_);
        // Assemble renderables from geometry+program bindings now that
        // scene_nodes_ is populated (#213 increment 1b): create the matching RHI
        // renderable per geometry node + set renderable_asset, the render program
        // inherited down the scene tree. The created assets compile in the shared
        // commit()+resolve_all() below (alongside the GLB scene sources).
        const std::size_t render_bindings_assembled =
            assemble_render_bindings(graph_draft_);
        // Resolve GLB scene-source DESCRIPTORS now that scene_nodes_ is populated
        // (#213, the descriptor route): register each glb_scene_source's GLB +
        // produced Scene asset and write the Scene key into the node's
        // scene_source. The scene-from-json commit/resolve above already ran
        // (descriptors live on scene_nodes_, only available now), so compile the
        // freshly registered GLB scenes with their own commit() + resolve_all()
        // before grafting. Same content => same key => cache hit on re-load.
        const std::size_t glb_sources_resolved = resolve_glb_scene_sources();
        if (glb_sources_resolved > 0 || render_bindings_assembled > 0) {
            ctx_.assets->commit();
            const wz::engine::assets::ResolveReport glb_resolve =
                ctx_.assets->resolve_all();
            if (!glb_resolve.ok()) {
                ctx_.logger.warn(
                    "load_scene: GLB scene-source / render-binding asset(s) "
                    "resolved with errors="
                    + std::to_string(glb_resolve.failures.size())
                    + " (loaded anyway for editor recovery)");
            }
        }
        // Bridge scene-source references to live Scene keys, then graft each
        // referenced sub-scene's GLB-named nodes as children of its host (#213,
        // instance mode) so they render and are behavior-addressable. The bridge
        // only touches nodes with scene_source_node_id, so it leaves the keys
        // resolve_glb_scene_sources just set on glb_scene_source nodes intact.
        const uint32_t scene_sources_bridged =
            wz::engine::assets::bridge_scene_source_keys(
                scene_nodes_, graph_draft_);
        // Re-point authored collision references at the bound graph's collision
        // keys (issue #216/#217), mirroring the renderable/source bridges; the
        // graph is committed by now (materialize ran above), so the referenced
        // collision node's key resolves.
        wz::engine::assets::bridge_scene_collision_keys(
            scene_nodes_, graph_draft_);
        // Flatten any glb_scene_source node authored with consume_mode=Flatten:
        // expand persistently (and drop the descriptor), exactly like the editor
        // "bake" action, so a Flatten-authored scene loads as real nodes. The
        // remaining Instance descriptors are grafted as live children below.
        for (const wz::engine::assets::SceneNodeAsset& node :
             std::vector<wz::engine::assets::SceneNodeAsset>(scene_nodes_))
        {
            if (node.glb_scene_source
                && node.glb_scene_source->consume_mode
                    == wz::engine::assets::SceneSourceConsumeMode::Flatten
                && node.scene_source)
            {
                flatten_scene_source(node.id);
            }
        }
        const std::size_t grafted = graft_scene_sources();
        if (scene_sources_bridged > 0 || grafted > 0) {
            ctx_.logger.info(
                "load_scene: scene sources bridged="
                + std::to_string(scene_sources_bridged)
                + " grafted children=" + std::to_string(grafted));
        }
        // Grafted children carry intrinsic GLB-part geometry bindings (#213
        // increment 3); assemble them now (the pre-graft assemble above ran before
        // these nodes existed) so the subtree renders under the host's program.
        if (grafted > 0) {
            rematerialize_render_bindings();
        }
        if (!scene_resolve.ok()) {
            ctx_.logger.warn(
                "load_scene: scene resolved with errors="
                + std::to_string(scene_resolve.failures.size())
                + " (loaded anyway for editor recovery)");
        }
        ctx_.logger.info(
            "load_scene: scene resolved (nodes="
            + std::to_string(authored_scene_node_count)
            + ", renderables bridged=" + std::to_string(bridged) + ")");

        // Load the project's behavior-module DLLs (if any) and register their
        // modules, then materialize the scene's behavior runtime. Mirrors
        // game_app's load -> register sequence; here the modules come from the
        // project manifest's behavior_module_folder rather than only built-ins.
        load_behavior_modules(desc.behavior_module_folder);
        rebuild_behavior_scene();

        // Auto-register the project's scenelets as spawnable prefabs (runtime
        // prefab spawning): each <resource_root>/scenelets/*.scene.json becomes a
        // prefab named by its stem, so a prefab_spawner behavior can graft it on a
        // SPAWN_PREFAB command. Silent no-op when the project has no scenelets.
        register_scenelet_prefabs();

        // One-shot scene-load lifecycle pass: lets a scene-setup behavior pick
        // the active camera (WZ_EVENT_SCENE_LOADED -> SET_ACTIVE_CAMERA) before
        // the first frame renders. Runs only on initial load, not on the
        // rebuild_behavior_scene calls that follow structural edits.
        select_scene_loaded_active_camera();

        // One-shot: in play mode, open the device and auto-play scene audio.
        start_scene_audio();

        return graph_ok && scene_resolve.ok();
    }

    void WozzitsApp_v1::simulation_tick(
        const wz::input::InputState& input, float dt, bool drive_camera)
    {
        // The fly-cam consumes input only when the host arms it (drive_camera);
        // behaviors below always get the input, so a controller can drive the
        // scene without panning the camera. aspect tracking is independent.
        if (drive_camera) {
            wz::bench::update_flying_camera(camera_, input, dt);
        }
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

        // Both camera sources are now current (free-fly updated from input above;
        // behaviors moved the scene-camera node in dispatch_scene_behaviors).
        // Materialize the single active view render_scene reads -- no work happens
        // in the render path.
        update_active_view();

        // Per-tick audio spatialization (play mode only, and only when the audio
        // device is actually running). Retunes already-playing Clip AudioSources
        // from the active listener's pose: pan + ITD + distance + Doppler. It
        // never starts a voice, so it's a harmless no-op for finished one-shots.
        // Needs the behavior scene (the runtime audio_sources/audio_listeners +
        // the runtime→authored map) and the nodes' world transforms.
        if (prefer_scene_camera_ && audio_runtime_.running() && behavior_scene_
            && ctx_.assets) {
            // #221: the pass reads source/listener world poses straight from the
            // behavior scene's polytree (the same single source of truth
            // scene_world_transforms() draws from), so it needs neither the
            // scene_nodes_ span nor a precomputed world-transform vector here.
            wz::engine::audio::update_scene_audio_spatialization(
                *ctx_.assets, *behavior_scene_,
                dt, audio_runtime_.output_sample_rate(),
                audio_runtime_.scheduler(), audio_spatialization_);
        }
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

    std::vector<SceneletCatalogEntry> WozzitsApp_v1::scenelet_catalog() const
    {
        return scenelet_catalog_;
    }

    std::size_t WozzitsApp_v1::register_scenelet_prefabs()
    {
        // Rebuilt from scratch on every (re)load, so a reload reflects added/removed
        // scenelet files.
        scenelet_catalog_.clear();
        if (!ctx_.assets) {
            return 0;
        }

        // Scenelets live in a "scenelets" folder next to the scene file (the
        // project root), so derive the folder from the scene's directory and
        // resolve it through the asset file system the way load_behavior_modules
        // resolves the module folder (resolve_path joins a relative path onto the
        // absolute resource root). A missing/empty folder is a silent no-op —
        // projects without scenelets register nothing.
        const wz::fs::Path project_dir = wz::fs::parent_path(scene_source_path_);
        const wz::fs::Path scenelets_rel =
            project_dir.empty()
                ? wz::fs::Path{ "scenelets" }
                : wz::fs::join(project_dir, "scenelets");
        const wz::fs::Path scenelets_dir =
            ctx_.assets->files().resolve_path(scenelets_rel);

        const wz::fs::FileResult<std::vector<wz::fs::DirEntry>> listing =
            wz::fs::list_directory(scenelets_dir);
        if (!listing) {
            return 0;  // no scenelets folder (or unreadable): nothing to register
        }

        std::size_t registered = 0;
        for (const wz::fs::DirEntry& entry : listing.value) {
            if (entry.is_directory) {
                continue;
            }
            // Match "*.scene.json" only (the scene-file convention). The prefab
            // name is the filename stem with the ".scene" suffix stripped, so
            // tank.scene.json -> "tank". wz::fs::extension returns the suffix
            // WITHOUT a leading dot.
            const wz::fs::Path full = wz::fs::join(scenelets_dir, entry.name);
            if (wz::fs::extension(full) != "json") {
                continue;
            }
            wz::fs::Path name = wz::fs::stem(full);  // "tank.scene"
            if (wz::fs::extension(name) != "scene") {
                continue;
            }
            name = wz::fs::stem(name);  // "tank"

            const wz::fs::FileResult<std::string> text =
                wz::fs::read_file_text(full);
            if (!text) {
                ctx_.logger.warn(
                    "register_scenelet_prefabs: could not read '"
                    + entry.name + "' (skipped)");
                continue;
            }

            wz::json::JSONParseResult parsed =
                wz::json::parse_json_string(text.value);
            if (!parsed.ok || !parsed.document.root) {
                ctx_.logger.warn(
                    "register_scenelet_prefabs: '" + entry.name
                    + "' is not valid JSON (skipped)");
                continue;
            }

            std::optional<wz::engine::assets::SceneAssetData> scene_data =
                wz::engine::assets::internal::parse_scene_data_from_json(
                    parsed.document, ctx_.logger);
            if (!scene_data) {
                ctx_.logger.warn(
                    "register_scenelet_prefabs: '" + entry.name
                    + "' did not parse as a scene (skipped)");
                continue;
            }

            register_prefab(name, std::move(scene_data->nodes));
            // Record it for the editor's scenelet menu (name + resource-relative
            // path, so the editor can open the file directly).
            scenelet_catalog_.push_back(SceneletCatalogEntry{
                .name = name,
                .path = wz::fs::join(scenelets_rel, entry.name),
            });
            ++registered;
        }

        if (registered > 0) {
            ctx_.logger.info(
                "register_scenelet_prefabs: registered "
                + std::to_string(registered) + " prefab(s) from "
                + scenelets_dir);
        }
        return registered;
    }

    std::size_t WozzitsApp_v1::resolve_glb_scene_sources()
    {
        if (!ctx_.assets) {
            return 0;
        }

        std::size_t resolved = 0;
        for (wz::engine::assets::SceneNodeAsset& node : scene_nodes_) {
            if (!node.glb_scene_source) {
                continue;
            }
            const wz::engine::assets::SceneGLBSceneSource& src =
                *node.glb_scene_source;
            if (src.path.empty()) {
                ctx_.logger.warn(
                    "resolve_glb_scene_sources: node '" + node.id
                    + "' has an empty GLB scene-source path (skipped)");
                node.scene_source.reset();
                continue;
            }

            // Mirror SceneFromGLBDesc from the descriptor. create_scene_from_glb
            // registers the GLB file itself, the per-mesh meshes/styled
            // renderables, and the Scene asset, returning the resolved Scene key.
            wz::engine::assets::SceneFromGLBDesc desc{};
            desc.name = "glb_scene_source/" + node.id;
            desc.path = src.path;
            desc.scene_index = src.scene_index;
            desc.base_style = src.base_style;
            desc.style_overrides.reserve(src.style_overrides.size());
            for (const auto& ov : src.style_overrides) {
                desc.style_overrides.push_back(
                    wz::engine::assets::SceneFromGLBStyleOverride{
                        .mesh_index = ov.mesh_index,
                        .style = ov.style,
                    });
            }

            const wz::engine::assets::SceneAsset scene =
                ctx_.assets->scenes().create_scene_from_glb(desc);
            if (!scene.valid()) {
                ctx_.logger.error(
                    "resolve_glb_scene_sources: create_scene_from_glb failed "
                    "for node '" + node.id + "' (path '" + src.path + "')");
                // Clear so a stale key from a previous resolve does not graft.
                node.scene_source.reset();
                continue;
            }

            node.scene_source = scene.output;
            ++resolved;
        }

        if (resolved > 0) {
            ctx_.logger.info(
                "resolve_glb_scene_sources: resolved "
                + std::to_string(resolved)
                + " GLB scene-source descriptor(s) to Scene keys");
        }
        return resolved;
    }

    std::size_t WozzitsApp_v1::assemble_render_bindings(
        const wz::asset::AssetGraphDraft& draft)
    {
        if (!ctx_.assets) {
            return 0;
        }

        // Resolve a graph node id to its committed output key + asset type,
        // mirroring bridge_scene_renderable_keys.
        const auto resolve_graph_node =
            [&draft](
                wz::asset::AssetGraphDraftNodeId id,
                wz::asset::AssetKey& out_key,
                wz::asset::AssetType& out_type) -> bool {
                const auto it = draft.node_index.find(id);
                if (it == draft.node_index.end()) {
                    return false;
                }
                out_key = draft.nodes[it->second].node.key;
                out_type = draft.nodes[it->second].node.type;
                return true;
            };

        // Nearest scene node with the given id (linear scan; scenes are small).
        const auto find_node =
            [this](const std::string& id)
                -> const wz::engine::assets::SceneNodeAsset* {
                for (const auto& n : scene_nodes_) {
                    if (n.id == id) {
                        return &n;
                    }
                }
                return nullptr;
            };

        std::size_t assembled = 0;
        for (wz::engine::assets::SceneNodeAsset& node : scene_nodes_) {
            // Resolve this node's OWN program ref (companion key), if any --
            // independent of whether the node itself draws.
            node.render_program_asset.reset();
            if (node.render_program_node_id) {
                wz::asset::AssetKey k{};
                wz::asset::AssetType t{};
                if (resolve_graph_node(*node.render_program_node_id, k, t)) {
                    node.render_program_asset = k;
                }
            }

            // Resolve an AudioSource's stable renderable anchor to its compiled
            // key (mirrors the visual renderable_asset_node_id bridge). Runtime
            // playback resolves via the key, so a node-id-only source is silent
            // until this fills it in. Independent of whether the node draws.
            if (node.audio_source
                && node.audio_source->audio_renderable_node_id) {
                node.audio_source->audio_renderable = {};  // clear stale
                wz::asset::AssetKey k{};
                wz::asset::AssetType t{};
                if (resolve_graph_node(
                        *node.audio_source->audio_renderable_node_id, k, t)) {
                    if (t == wz::engine::assets::kAssetTypeAudioRenderable) {
                        node.audio_source->audio_renderable = k;
                    }
                    else {
                        ctx_.logger.warn(
                            "assemble_render_bindings: node '" + node.id
                            + "' audio_renderable anchor is not an audio "
                              "renderable (skipped)");
                    }
                }
            }

            if (!node.geometry_asset_node_id) {
                continue;  // no geometry -> draws nothing via the binding
            }

            // Geometry present: the binding owns this node's renderable. Clear
            // any stale/pre-built key first so a removed geometry stops drawing.
            node.geometry_asset.reset();
            node.renderable_asset.reset();

            wz::asset::AssetKey geometry_key{};
            wz::asset::AssetType geometry_type{};
            if (!resolve_graph_node(
                    *node.geometry_asset_node_id, geometry_key, geometry_type))
            {
                ctx_.logger.warn(
                    "assemble_render_bindings: node '" + node.id
                    + "' geometry asset-graph node not found (skipped)");
                continue;
            }

            // Indexed GLB-part geometry (issue #213 increment 3): the referenced
            // node is a "Scene from GLB" output; extract this node's named part as
            // a Mesh and route it like any pull mesh. One Scene-from-GLB node feeds
            // every part by name (no per-part extractor graph node).
            if (node.geometry_glb_node_id) {
                const auto part = ctx_.assets->meshes().create_mesh_from_glb_scene(
                    wz::engine::assets::MeshFromGLBSceneDesc{
                        .name = "render_binding_part/" + node.id,
                        .scene = geometry_key,
                        .node_id = *node.geometry_glb_node_id,
                    });
                if (!part.valid()) {
                    ctx_.logger.warn(
                        "assemble_render_bindings: node '" + node.id
                        + "' could not extract GLB part '"
                        + *node.geometry_glb_node_id + "' (skipped)");
                    continue;
                }
                geometry_key = part.output;
                geometry_type = wz::engine::assets::kAssetTypeMesh;
            }
            node.geometry_asset = geometry_key;

            // Effective render program: this node's own, else the nearest
            // ancestor up parent_id that has one (inheritance down the tree).
            // Bounded by the node count so a dangling/cyclic parent chain can't
            // hang.
            wz::asset::AssetKey program_key{};
            bool has_program = false;
            const wz::engine::assets::SceneNodeAsset* cur = &node;
            for (std::size_t guard = 0;
                 cur && guard <= scene_nodes_.size();
                 ++guard)
            {
                if (cur->render_program_node_id) {
                    wz::asset::AssetKey k{};
                    wz::asset::AssetType t{};
                    if (resolve_graph_node(*cur->render_program_node_id, k, t)) {
                        program_key = k;
                        has_program = true;
                    }
                    break;  // nearest authored program wins, even if unresolved
                }
                if (!cur->parent_id || cur->parent_id->empty()) {
                    break;
                }
                cur = find_node(*cur->parent_id);
            }
            if (!has_program) {
                ctx_.logger.warn(
                    "assemble_render_bindings: node '" + node.id
                    + "' has geometry but no render program (own or inherited);"
                      " it will not draw");
                continue;
            }

            // Route by the geometry asset's type to the matching RHI renderable.
            wz::engine::assets::RenderableAsset renderable{};
            if (geometry_type == wz::engine::assets::kAssetTypeMesh) {
                renderable = ctx_.assets->renderables().create_rhi_pull_mesh(
                    wz::engine::assets::RhiPullMeshRenderableDesc{
                        .name = "render_binding/" + node.id,
                        .mesh = wz::engine::assets::MeshAsset{
                            .output = geometry_key },
                        .program = wz::engine::assets::RenderProgramAsset{
                            .key = program_key },
                    });
            }
            else if (geometry_type
                     == wz::engine::assets::kAssetTypeGpuSparseMesh)
            {
                renderable =
                    ctx_.assets->renderables().create_gpu_sparse_mesh_renderable(
                        wz::engine::assets::GpuSparseMeshRenderableDesc{
                            .name = "render_binding/" + node.id,
                            .sparse_mesh =
                                wz::engine::assets::GpuSparseMeshAsset{
                                    .output = geometry_key },
                            .program = wz::engine::assets::RenderProgramAsset{
                                .key = program_key },
                        });
            }
            else {
                ctx_.logger.warn(
                    "assemble_render_bindings: node '" + node.id
                    + "' geometry has an unsupported type for a renderable "
                      "binding (type "
                    + std::to_string(static_cast<uint32_t>(geometry_type))
                    + "); it will not draw");
                continue;
            }

            if (!renderable.valid()) {
                ctx_.logger.error(
                    "assemble_render_bindings: failed to create a renderable "
                    "for node '" + node.id + "'");
                continue;
            }
            node.renderable_asset = renderable.output;
            ++assembled;
        }

        if (assembled > 0) {
            ctx_.logger.info(
                "assemble_render_bindings: assembled "
                + std::to_string(assembled)
                + " renderable(s) from geometry+program bindings");
        }
        return assembled;
    }

    std::size_t WozzitsApp_v1::graft_scene_sources()
    {
        if (!ctx_.assets) {
            return 0;
        }

        // Idempotent re-graft: drop the previously grafted children first so a
        // re-bind/re-resolve (new Scene keys) or a scene-source edit re-expands
        // cleanly rather than accumulating duplicates.
        if (!grafted_node_ids_.empty()) {
            std::unordered_set<std::string> stale(
                grafted_node_ids_.begin(), grafted_node_ids_.end());
            scene_nodes_.erase(
                std::remove_if(
                    scene_nodes_.begin(),
                    scene_nodes_.end(),
                    [&stale](const wz::engine::assets::SceneNodeAsset& n) {
                        return stale.count(n.id) != 0;
                    }),
                scene_nodes_.end());
            grafted_node_ids_.clear();
        }

        // Snapshot the host nodes carrying a resolved scene_source up front: we
        // mutate scene_nodes_ (append children) while iterating, and grafted
        // children never themselves carry a scene_source (the expander clears
        // it), so a single non-recursive pass over the current hosts is enough.
        struct HostRef
        {
            wz::engine::assets::SceneNodeAsset host;  // copy (stable across append)
            wz::asset::AssetKey scene_source;
        };
        std::vector<HostRef> hosts;
        for (const wz::engine::assets::SceneNodeAsset& node : scene_nodes_) {
            if (node.scene_source) {
                hosts.push_back(HostRef{ node, *node.scene_source });
            }
        }

        std::size_t grafted = 0;
        for (const HostRef& ref : hosts) {
            const wz::engine::assets::SceneHandle handle =
                ctx_.assets->scenes().get_scene(
                    wz::engine::assets::SceneAsset{ .output = ref.scene_source });
            const wz::engine::assets::SceneAssetData* sub =
                ctx_.assets->scenes().get_scene_data(handle);
            if (!sub) {
                ctx_.logger.warn(
                    "graft_scene_sources: scene_source for node '" + ref.host.id
                    + "' did not resolve to a Scene asset (skipped)");
                continue;
            }

            std::vector<wz::engine::assets::SceneNodeAsset> children =
                wz::engine::assets::expand_scene_source_children(
                    ref.host, *sub);
            // Re-apply this host's sticky per-child component overrides (issue
            // #213) onto the freshly expanded children, keyed by the child's
            // sub-scene id ("<host>/" suffix). Applied AFTER expansion so the
            // authored program rides on the runtime child without being folded
            // into the GLB Scene key.
            const std::string host_prefix = ref.host.id + "/";
            std::unordered_set<std::string> matched_overrides;
            for (wz::engine::assets::SceneNodeAsset& child : children) {
                if (child.id.size() > host_prefix.size()
                    && child.id.compare(
                           0, host_prefix.size(), host_prefix) == 0)
                {
                    const std::string sub_id =
                        child.id.substr(host_prefix.size());
                    for (const wz::engine::assets::SceneSourceChildOverride& ov :
                         ref.host.scene_source_child_overrides)
                    {
                        if (ov.child_id != sub_id) {
                            continue;
                        }
                        if (ov.render_program_node_id) {
                            child.render_program_node_id =
                                ov.render_program_node_id;
                        }
                        matched_overrides.insert(sub_id);
                        break;
                    }
                }
                grafted_node_ids_.push_back(child.id);
                scene_nodes_.push_back(std::move(child));
                ++grafted;
            }
            // Sticky policy: an override whose child_id no longer expands (renamed
            // / removed source node, or a transient resolve failure) is RETAINED,
            // never deleted — only logged, so a later source fix re-attaches it.
            for (const wz::engine::assets::SceneSourceChildOverride& ov :
                 ref.host.scene_source_child_overrides)
            {
                if (matched_overrides.count(ov.child_id) == 0) {
                    ctx_.logger.info(
                        "graft_scene_sources: child override '" + ov.child_id
                        + "' under host '" + ref.host.id
                        + "' matched no expanded node (retained)");
                }
            }
        }

        if (grafted > 0) {
            ctx_.logger.info(
                "graft_scene_sources: grafted "
                + std::to_string(grafted) + " scene-source child node(s) from "
                + std::to_string(hosts.size()) + " host(s)");
        }
        return grafted;
    }

    void WozzitsApp_v1::capture_grafted_child_override(
        const wz::scene::AuthoredEntityId& child_id)
    {
        // Only runtime-only grafted children need a sticky override; authored
        // nodes persist their components directly.
        if (std::find(
                grafted_node_ids_.begin(), grafted_node_ids_.end(), child_id)
            == grafted_node_ids_.end())
        {
            return;
        }
        const wz::engine::assets::SceneNodeAsset* child =
            wz::engine::assets::find_scene_node(scene_nodes_, child_id);
        if (!child) {
            return;
        }

        // Walk up to the owning host: the first non-grafted ancestor (the node
        // that carries the scene source this child was expanded from). All grafted
        // children are descendants of their host, and the host is not grafted.
        const std::unordered_set<std::string> grafted(
            grafted_node_ids_.begin(), grafted_node_ids_.end());
        wz::engine::assets::SceneNodeAsset* host = nullptr;
        const wz::engine::assets::SceneNodeAsset* cursor = child;
        for (std::size_t guard = 0;
             cursor && guard <= scene_nodes_.size();
             ++guard)
        {
            if (!cursor->parent_id || cursor->parent_id->empty()) {
                break;
            }
            wz::engine::assets::SceneNodeAsset* parent =
                wz::engine::assets::find_scene_node(
                    scene_nodes_, *cursor->parent_id);
            if (!parent) {
                break;
            }
            if (grafted.count(parent->id) == 0) {
                host = parent;  // first non-grafted ancestor = host
                break;
            }
            cursor = parent;
        }
        if (!host) {
            return;
        }

        // Sub-scene-relative key: strip the "<host>/" prefix the graft namespaced
        // it with (child.id == host.id + "/" + src.id).
        const std::string prefix = host->id + "/";
        if (child_id.size() <= prefix.size()
            || child_id.compare(0, prefix.size(), prefix) != 0)
        {
            return;
        }
        const std::string sub_id = child_id.substr(prefix.size());

        // Upsert the override from the child's CURRENT authored state; an override
        // that would carry nothing is erased (so clearing a child's program drops
        // the entry rather than persisting an empty one).
        std::vector<wz::engine::assets::SceneSourceChildOverride>& overrides =
            host->scene_source_child_overrides;
        const auto it = std::find_if(
            overrides.begin(),
            overrides.end(),
            [&sub_id](const wz::engine::assets::SceneSourceChildOverride& ov) {
                return ov.child_id == sub_id;
            });

        wz::engine::assets::SceneSourceChildOverride next{};
        next.child_id = sub_id;
        next.render_program_node_id = child->render_program_node_id;

        const bool carries_nothing = !next.render_program_node_id.has_value();
        if (carries_nothing) {
            if (it != overrides.end()) {
                overrides.erase(it);
                scene_dirty_ = true;
            }
            return;
        }
        if (it != overrides.end()) {
            *it = std::move(next);
        }
        else {
            overrides.push_back(std::move(next));
        }
        scene_dirty_ = true;
    }

    void WozzitsApp_v1::rebuild_behavior_scene()
    {
        // DIAGNOSTIC (#219): a rebuild while the scene camera is the active source
        // renumbers polytree handles; refresh_active_camera_entity re-seats the
        // handle below, but logging the event tells us whether an unexpected
        // per-frame rebuild (not pure movement) is what correlates with the flip.
        if (camera_source_ == CameraSource::Scene) {
            ctx_.logger.warn(
                "rebuild_behavior_scene while scene camera active (anchor='"
                + active_camera_id_ + "')");
        }

        // Capture the outgoing instance's behavior state BEFORE the reset so a
        // rebuild (structural edit / prefab spawn) preserves every pre-existing
        // binding's BehaviorStateBlock: instance state is keyed by binding id, and
        // a binding id is re-derived from the node id (effective_behavior_binding_id),
        // so a binding present in both the old and rebuilt scenes keeps the SAME
        // key. Moving the maps into the rebuilt instance before initialize_behaviors
        // means on_init's wz_get_instance_state finds the preserved block and does
        // NOT re-construct it (state survives), while a newly added binding has no
        // block yet and initializes fresh. Carried for both instance and shared
        // state. Default-empty when there was no prior instance.
        wz::engine::assets::BehaviorStateStorage preserved_state;
        // Behavior-set runtime Motion overrides do NOT live on the authored asset,
        // so reinstantiating below would wipe them. Capture the terrain-alignment
        // rate keyed by STABLE authored node id (runtime ids are renumbered on
        // rebuild) and restore it after, so a surviving actor keeps the rate a
        // self.start handler set -- without that, a spawn would reset every existing
        // actor to instant alignment. Keyed by authored id, mirroring how
        // behavior_state survives by binding id.
        std::unordered_map<std::string, float> preserved_alignment_rate;
        // #221: sim-accumulated LOCAL transforms don't live on the authored asset
        // either — scene_nodes_ stays at its authored pose now that the per-frame
        // write-back is gone. Rebuilding materializes the polytree from
        // scene_nodes_, which would snap every actor back to its authored start
        // (losing the motion a surviving binding accrued). Capture each live
        // node's LOCAL matrix keyed by STABLE authored id and restore it after
        // materialization, so a binding present in both the old and rebuilt scenes
        // continues from where it was — the same "state survives rebuild" contract
        // behavior_state and alignment_rate keep. A newly spawned/added node has no
        // entry and materializes at its authored transform.
        std::unordered_map<std::string, wz::math::Mat4> preserved_local;
        if (behavior_scene_) {
            preserved_state = std::move(behavior_scene_->behavior_state);
            const std::size_t live_node_count =
                wz::core::graph::node_count(behavior_scene_->storage.polytree);
            for (std::size_t entity = 0;
                 entity < behavior_scene_->runtime_to_authored.size()
                     && entity < live_node_count;
                 ++entity)
            {
                preserved_local[behavior_scene_->runtime_to_authored[entity]] =
                    wz::core::graph::node_data(
                        behavior_scene_->storage.polytree, entity).local;
            }
            for (const auto& record : behavior_scene_->motions) {
                if (record.component.terrain_alignment_rate != 0.0f
                    && record.node
                        < behavior_scene_->runtime_to_authored.size())
                {
                    preserved_alignment_rate[
                        behavior_scene_->runtime_to_authored[record.node]] =
                        record.component.terrain_alignment_rate;
                }
            }
        }

        behavior_scene_.reset();

        // #221: materialize the runtime SceneInstance for EVERY loaded scene, not
        // just ones that carry a behavior/motion/terrain/constraint. The polytree
        // is now the single source of truth for the drawn + saved + edited world
        // transforms (see scene_world_transforms / apply_node_local_transform), so
        // even a fully static scene needs its polytree live — a transform edit
        // lands there and the renderer reads it. A static scene simply comes up
        // with empty behavior/motion/terrain tables; simulation_tick's sub-passes
        // iterate those tables, so they are cheap no-ops (nothing to integrate or
        // constrain). behavior_scene_-null tolerance survives in every consumer as
        // a DEFENSIVE fallback for the one path that still leaves it null: a
        // FAILED instantiate_scene (a malformed scene), below.

        // Materialize a runtime SceneInstance from the authored scene. The
        // renderable resolver is intentionally null: WozzitsApp_v1 renders from
        // scene_nodes_, so the instance is used only for its polytree + behavior
        // tables (renderables in the instance are unused). Because instantiate_
        // scene FAILS a node that carries a renderable_asset when no resolver is
        // provided, strip the renderable references from the instance's node
        // copies first — otherwise a renderable-carrying node (notably the
        // GLB-grafted scene-source children, #213) would fail materialization and
        // the whole behavior runtime (incl. those children's addressability)
        // would not come up. The strip is on the COPY only; scene_nodes_ (the
        // renderer's source of truth) keeps its renderables.
        wz::engine::assets::SceneAssetData scene_data;
        scene_data.nodes = scene_nodes_;
        for (wz::engine::assets::SceneNodeAsset& node : scene_data.nodes) {
            node.renderable.reset();
            node.renderable_asset.reset();
            node.renderable_asset_node_id.reset();
        }

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
            refresh_active_camera_entity();
            return;
        }

        behavior_scene_ = std::move(instantiated.instance);

        // Carry the preserved state into the rebuilt instance BEFORE init, so a
        // binding that survives the rebuild keeps its block (and skips re-
        // construction); a new binding has no block and initializes fresh.
        behavior_scene_->behavior_state = std::move(preserved_state);

        // Restore behavior-set runtime alignment rates onto the rebuilt motions
        // (matched by authored node id). A motion not in the map keeps its
        // default 0; a newly spawned actor gets its rate from the self.start pass
        // below instead.
        if (!preserved_alignment_rate.empty()) {
            for (auto& record : behavior_scene_->motions) {
                if (record.node
                    >= behavior_scene_->runtime_to_authored.size())
                {
                    continue;
                }
                const auto it = preserved_alignment_rate.find(
                    behavior_scene_->runtime_to_authored[record.node]);
                if (it != preserved_alignment_rate.end()) {
                    record.component.terrain_alignment_rate = it->second;
                }
            }
        }

        // #221: restore the captured sim-accumulated LOCAL transforms onto the
        // rebuilt polytree (matched by authored id), then re-propagate so the
        // world matrices reflect them. A node not in the map (newly added/spawned)
        // keeps the authored local it just materialized with. This preserves the
        // pre-#221 semantic that a surviving actor continues from its accrued pose
        // across a rebuild — the write-back used to achieve it by keeping
        // scene_nodes_ sim-current; now the transform is carried directly.
        if (!preserved_local.empty()) {
            const std::size_t live_node_count =
                wz::core::graph::node_count(behavior_scene_->storage.polytree);
            for (std::size_t entity = 0;
                 entity < behavior_scene_->runtime_to_authored.size()
                     && entity < live_node_count;
                 ++entity)
            {
                const auto it = preserved_local.find(
                    behavior_scene_->runtime_to_authored[entity]);
                if (it != preserved_local.end()) {
                    const_cast<wz::scene::TransformNode&>(
                        wz::core::graph::node_data(
                            behavior_scene_->storage.polytree, entity))
                        .local = it->second;
                }
            }
            wz::scene::propagate_all(behavior_scene_->storage.polytree);
        }

        // Initialize behaviors (init callbacks + per-binding/shared state) once
        // for the materialized scene, exactly as game_app does after building
        // its scene.
        wz::engine::behavior::initialize_behaviors(
            *behavior_scene_, registry_, &ctx_.logger);

        // Prune carried-over instance-state blocks whose binding id is no longer
        // present among the rebuilt scene's bindings (a removed/renamed node, or a
        // node never re-added). Without this, those blocks would leak across every
        // rebuild. Shared state is keyed independently of nodes (it is created on
        // demand by key), so it is NOT pruned here — it is carried as-is.
        {
            std::unordered_set<std::string> live_binding_ids;
            live_binding_ids.reserve(behavior_scene_->behaviors.size());
            for (const auto& record : behavior_scene_->behaviors) {
                live_binding_ids.insert(record.component.binding_id);
            }
            auto& instance_state =
                behavior_scene_->behavior_state.instance_state;
            for (auto it = instance_state.begin();
                 it != instance_state.end();)
            {
                if (live_binding_ids.find(it->first)
                    == live_binding_ids.end())
                {
                    it = instance_state.erase(it);
                } else {
                    ++it;
                }
            }

            // Same hygiene for the cognition wake schedule: drop wakes for
            // bindings that no longer exist so they do not leak across rebuilds.
            auto& next_wakes = behavior_scene_->behavior_state.next_wakes;
            for (auto it = next_wakes.begin(); it != next_wakes.end();) {
                if (live_binding_ids.find(it->first)
                    == live_binding_ids.end())
                {
                    it = next_wakes.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Release cognition wave functions whose quantum_agent binding vanished
        // this rebuild (a despawned NPC, a removed node, a scene swap). The store
        // outlives the scene and keys agents by an opaque handle the binding's POD
        // state carries, so sweep it against the surviving quantum_agent handles.
        {
            std::vector<wz::engine::cognition::AgentHandle> live_agents;
            for (const auto& record : behavior_scene_->behaviors) {
                if (record.component.module
                    != wz::engine::behavior::kQuantumAgentModule)
                {
                    continue;
                }
                const auto* block =
                    behavior_scene_->behavior_state.find_instance_state(
                        record.component.binding_id);
                if (block && block->data) {
                    const auto* state = static_cast<
                        const wz::engine::behavior::QuantumAgentState*>(
                            block->data);
                    if (state->handle != 0u) {
                        live_agents.push_back(state->handle);
                    }
                }
            }
            wz::engine::behavior::quantum_agent_store().retain(live_agents);
        }

        ctx_.logger.info(
            "behavior scene initialized (bindings="
            + std::to_string(behavior_scene_->behaviors.size()) + ")");

        // One-shot self.start lifecycle pass: fire WZ_EVENT_SELF_START for every
        // binding that has not started yet (tracked in behavior_state, preserved
        // across rebuilds) and apply the commands it produces. On initial load
        // this fires for all subscribed authored bindings; on a prefab spawn or a
        // live edit it fires ONLY for the newly materialized bindings -- existing
        // actors keep their started flag (and their preserved runtime overrides),
        // so nothing re-runs per spawn. A single integration point covers load,
        // spawn, and edit because they all funnel through rebuild_behavior_scene.
        {
            wz::engine::behavior::BehaviorCommandBuffer self_start_commands;
            wz::engine::behavior::BehaviorFrameContext self_start_ctx{
                .scene = &*behavior_scene_,
                .behavior_state = &behavior_scene_->behavior_state,
                .commands = &self_start_commands,
                .logger = &ctx_.logger,
            };
            wz::engine::behavior::dispatch_self_start(
                *behavior_scene_, registry_, self_start_ctx);
            if (!self_start_commands.commands.empty()) {
                std::vector<wz::scene::RuntimeEntityId> self_start_changed;
                (void)wz::engine::behavior::apply_behavior_commands(
                    *behavior_scene_,
                    self_start_commands.commands,
                    &self_start_changed);
            }
        }

        // Re-point the active camera handle at the rebuilt scene's entities, so a
        // selected scene camera survives a rebuild (e.g. a behavior spawning a
        // child) without falling back to free-fly.
        refresh_active_camera_entity();
    }

    void WozzitsApp_v1::select_scene_loaded_active_camera()
    {
        // One-shot WZ_EVENT_SCENE_LOADED pass: a scene-setup behavior (bound to
        // e.g. the scene root) may emit SET_ACTIVE_CAMERA naming a camera node.
        // Apply it here, before the first frame. The behavior only names the
        // node; the camera math lives here so the convention matches the
        // free-fly camera and the standalone driver hard-codes nothing.
        if (!behavior_scene_) {
            return;
        }

        frame_storage_.behavior_commands.clear();
        wz::engine::behavior::BehaviorFrameContext ctx{
            .frame_storage = &frame_storage_,
            .scene = &*behavior_scene_,
            .behavior_state = &behavior_scene_->behavior_state,
            .commands = &frame_storage_.behavior_commands,
            .logger = &ctx_.logger,
        };
        // Re-decide the active camera on every (re)load: drop the prior anchor
        // and fall back to the free-fly source until a scene-setup behavior
        // selects a camera (and, in play, prefer_scene_camera_ flips the source).
        active_camera_id_.clear();
        active_camera_entity_ = wz::scene::INVALID_RUNTIME_ENTITY;
        camera_source_ = CameraSource::FreeFly;
        wz::engine::behavior::dispatch_scene_loaded(
            *behavior_scene_, registry_, ctx);

        for (const wz::engine::behavior::BehaviorCommand& command :
             frame_storage_.behavior_commands.commands)
        {
            if (command.kind
                == wz::engine::behavior::BehaviorCommandKind::SetActiveCamera)
            {
                apply_scene_active_camera(command.entity);
            }
        }

        // Materialize the active view once now so the first render after a load
        // (before the first simulation_tick) draws through the selected camera.
        update_active_view();
    }

    void WozzitsApp_v1::register_prefab(
        const std::string& prefab_name,
        std::vector<wz::engine::assets::SceneNodeAsset> nodes)
    {
        // Key by the same FNV-1a/32 hash the behavior helpers use, so a
        // SPAWN_PREFAB command naming this prefab resolves here. A re-register
        // replaces the prior nodes (the cache is the live prefab set).
        const uint32_t hash = prefab_name_hash(prefab_name);
        prefab_by_hash_[hash] = std::move(nodes);
        ctx_.logger.info(
            "register_prefab: '" + prefab_name + "' ("
            + std::to_string(prefab_by_hash_[hash].size()) + " nodes)");
    }

    std::size_t WozzitsApp_v1::spawned_prefab_node_count() const
    {
        // The spawn graft mints ids with a "spawn:" prefix (instantiate_prefab_-
        // nodes); count them so a test can observe a prefab landing in scene_nodes_.
        std::size_t count = 0;
        for (const wz::engine::assets::SceneNodeAsset& node : scene_nodes_) {
            if (node.id.rfind("spawn:", 0) == 0) {
                ++count;
            }
        }
        return count;
    }

    const wz::scene::AuthoredEntityId&
    WozzitsApp_v1::active_scene_camera_id() const
    {
        return active_camera_id_;
    }

    void WozzitsApp_v1::spawn_prefab(
        const wz::scene::AuthoredEntityId& spawner_id,
        uint32_t prefab_name_hash,
        float offset_x,
        float offset_y,
        float offset_z)
    {
        // Resolve the prefab once (the registered scenelet for this name hash).
        const auto prefab_it = prefab_by_hash_.find(prefab_name_hash);
        if (prefab_it == prefab_by_hash_.end()) {
            ctx_.logger.warn(
                "spawn_prefab: unknown prefab hash "
                + std::to_string(prefab_name_hash) + " — skipped");
            return;
        }
        if (prefab_it->second.empty()) {
            ctx_.logger.warn("spawn_prefab: prefab has no nodes — skipped");
            return;
        }

        // Resolve the spawner's stable authored id -> its index in scene_nodes_ so
        // the spawn anchor is the spawner's live world transform. #221:
        // scene_world_transforms() is index-aligned with scene_nodes_ and, with a
        // live behavior scene, reads the sim-current pose from the polytree (this
        // drain runs after propagate_all). Addressing by authored id (not runtime
        // entity) keeps this valid across the rebuild a prior spawn in this drain
        // may have done.
        const std::vector<wz::math::Mat4> world_transforms =
            scene_world_transforms();
        wz::math::Mat4 spawner_world = wz::math::Mat4::identity();
        bool found_spawner = false;
        for (std::size_t i = 0; i < scene_nodes_.size(); ++i) {
            if (scene_nodes_[i].id == spawner_id) {
                spawner_world = world_transforms[i];
                found_spawner = true;
                break;
            }
        }
        if (!found_spawner) {
            ctx_.logger.warn(
                "spawn_prefab: spawner node '" + spawner_id
                + "' not in scene — skipped");
            return;
        }

        // T = spawner world transform × the offset. mul(a, b) = a * b
        // (column-major), so the offset is applied in the spawner's local frame
        // and then carried by the spawner's world transform — i.e. the prefab is
        // placed `offset` away from the spawner IN the spawner's frame. Decompose
        // to a TRS for the re-rooted prefab root's local (it becomes a top-level
        // node, so its local == this world transform).
        const wz::math::Mat4 spawn_world = wz::math::mul(
            spawner_world,
            wz::math::translation(
                wz::math::Vec3{ offset_x, offset_y, offset_z }));

        wz::math::Transform trs{};
        if (!wz::math::decompose_trs(spawn_world, trs)) {
            trs = wz::math::rigid_pose_from_matrix(spawn_world);
        }

        // Spawn LEVEL: replace the inherited rotation with an upright one facing
        // the spawn heading (yaw about world +Y). The spawner's full rotation
        // carries its OWN terrain-induced tilt (pitch/roll for ITS surface); at
        // the spawn location that tilt is wrong -- it tips the tank onto a side
        // edge and aims its local-forward partly into the ground, so it drives
        // under the surface. Keep only the heading (the spawn matrix's +Z axis
        // projected onto the horizontal plane) and let per-frame terrain
        // alignment tilt the tank correctly for its own surface.
        {
            const float fwd_x = spawn_world.m[8];   // local +Z in world (column 2)
            const float fwd_z = spawn_world.m[10];
            const float horiz = std::sqrt(fwd_x * fwd_x + fwd_z * fwd_z);
            if (horiz > 1.0e-4f) {
                const float yaw = std::atan2(fwd_x, fwd_z);  // +Z at yaw 0
                trs.rotation = wz::math::Quaternion{
                    0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f) };
            }
        }
        ctx_.logger.info(
            "spawn_prefab: spawn at ("
            + std::to_string(trs.position.x) + ", "
            + std::to_string(trs.position.y) + ", "
            + std::to_string(trs.position.z) + ") scale "
            + std::to_string(trs.scale.x));

        wz::engine::assets::AuthoredTransform root_transform{};
        root_transform.translation[0] = trs.position.x;
        root_transform.translation[1] = trs.position.y;
        root_transform.translation[2] = trs.position.z;
        root_transform.rotation_quat[0] = trs.rotation.x;
        root_transform.rotation_quat[1] = trs.rotation.y;
        root_transform.rotation_quat[2] = trs.rotation.z;
        root_transform.rotation_quat[3] = trs.rotation.w;
        root_transform.scale[0] = trs.scale.x;
        root_transform.scale[1] = trs.scale.y;
        root_transform.scale[2] = trs.scale.z;

        // Clone the prefab with conflict-free ids under the spawn transform, then
        // graft: append to scene_nodes_ (the renderer's + behavior runtime's
        // source of truth), rebuild the behavior runtime (now state-preserving, so
        // pre-existing bindings keep their state and the spawned subtree's
        // behaviors initialize fresh), and re-assemble render bindings (so a
        // spawned renderable draws). Mirrors add_child_node's append->rebuild.
        std::vector<wz::engine::assets::SceneNodeAsset> spawned =
            wz::engine::assets::instantiate_prefab_nodes(
                prefab_it->second, ++spawn_counter_, root_transform);
        scene_nodes_.insert(
            scene_nodes_.end(),
            std::make_move_iterator(spawned.begin()),
            std::make_move_iterator(spawned.end()));
        // A runtime spawn is ephemeral, NOT an authoring edit -- do not mark the
        // scene dirty (and save_scene excludes "spawn:" nodes regardless), so
        // spawned instances are never written back into scene.json.

        // Expand any scene_source (GLB) geometry the spawned subtree references
        // into grafted child nodes -- the SAME bridge + graft load_scene / bind
        // run. A prefab whose geometry comes from a scene_source (e.g. a GLB tank)
        // carries only the host node; its meshes are the grafted children, so
        // without this the host appends but nothing draws. bridge resolves the
        // spawned node's scene_source_node_id -> Scene key; graft_scene_sources is
        // idempotent (re-grafts every host) and prefixes child ids with the host
        // id, which is unique per spawn, so each instance gets its own children.
        if (ctx_.assets) {
            wz::engine::assets::bridge_scene_source_keys(
                scene_nodes_, graph_draft_);
            graft_scene_sources();
            // assemble_render_bindings resolves each spawned node's render_program
            // + AudioSource anchors (node id -> key) on scene_nodes_. It MUST run
            // BEFORE rebuild_behavior_scene, because the rebuild materializes the
            // runtime AudioSource from node.audio_source->audio_renderable -- an
            // unresolved (empty) key there makes a spawned tank's cannon silent
            // (mirrors the load path, where assemble precedes the final rebuild).
            assemble_render_bindings(graph_draft_);
        }

        rebuild_behavior_scene();
        if (ctx_.assets) {
            // rematerialize_render_bindings: the freshly grafted GLB children's
            // intrinsic geometry bindings (the pre-graft assemble can't see them).
            rematerialize_render_bindings();
        }
        ctx_.logger.info(
            "spawn_prefab: grafted prefab as instance "
            + std::to_string(spawn_counter_));
    }

    void WozzitsApp_v1::apply_scene_active_camera(
        wz::scene::RuntimeEntityId runtime_entity)
    {
        if (!behavior_scene_
            || runtime_entity >= behavior_scene_->runtime_to_authored.size())
        {
            return;
        }
        const wz::scene::AuthoredEntityId& authored_id =
            behavior_scene_->runtime_to_authored[runtime_entity];

        // One-shot selection: capture the camera's projection params from its
        // authored node (a single scan, here, not per frame) and remember the
        // node by id + live polytree handle. update_active_view() then reads the
        // node's already-maintained world transform through the handle.
        const auto it = std::find_if(
            scene_nodes_.begin(),
            scene_nodes_.end(),
            [&](const wz::engine::assets::SceneNodeAsset& node) {
                return node.id == authored_id;
            });
        if (it == scene_nodes_.end() || !it->camera) {
            ctx_.logger.warn(
                "scene_camera: node '" + authored_id
                + "' has no camera component; active camera unchanged");
            return;
        }

        active_camera_id_ = authored_id;
        active_camera_entity_ = runtime_entity;
        active_camera_params_ = *it->camera;

        // Play hosts (standalone) flip the active source to the scene camera; the
        // editor records the anchor but stays on the free-fly edit camera so you
        // can navigate (a later editor toggle can switch the source to Scene).
        if (prefer_scene_camera_) {
            camera_source_ = CameraSource::Scene;
        }
        ctx_.logger.info(
            "scene_camera: active camera selected on node '" + authored_id
            + "' (source="
            + (camera_source_ == CameraSource::Scene ? "scene" : "free-fly")
            + ")");
    }

    void WozzitsApp_v1::refresh_active_camera_entity()
    {
        if (active_camera_id_.empty() || !behavior_scene_) {
            active_camera_entity_ = wz::scene::INVALID_RUNTIME_ENTITY;
            return;
        }
        const auto it =
            behavior_scene_->authored_to_runtime.find(active_camera_id_);
        active_camera_entity_ = it != behavior_scene_->authored_to_runtime.end()
            ? it->second
            : wz::scene::INVALID_RUNTIME_ENTITY;
    }

    void WozzitsApp_v1::reload_behavior_modules(
        const wz::fs::Path& module_folder)
    {
        // Drop the previously loaded modules (built-ins + project DLLs) and the
        // plugin host's dynamic libraries, then rebuild from scratch. Clearing
        // first is required: load_behavior_modules only reloads same-path DLLs,
        // so without a clear a renamed/removed module would linger, and the
        // built-ins (registered only in the ctor) must be re-registered after
        // the registry clear or the scene would lose them.
        ctx_.logger.info(
            "reload_behavior_modules: reloading behavior modules from "
            + (module_folder.empty() ? std::string("<builtins only>")
                                     : std::string(module_folder)));
        registry_.clear();
        plugins_.clear();
        wz::engine::behavior::register_builtin_behaviors(
            registry_, plugins_, ctx_.logger);
        load_behavior_modules(module_folder);
        rebuild_behavior_scene();
    }

    std::vector<std::string> WozzitsApp_v1::behavior_module_names() const
    {
        std::vector<std::string> names;
        const auto modules = registry_.modules();
        names.reserve(modules.size());
        for (const auto& registration : modules) {
            names.push_back(registration.module);
        }
        return names;
    }

    void WozzitsApp_v1::dispatch_scene_behaviors(
        const wz::input::InputState& input, float dt)
    {
        // Run the per-frame simulation whenever a runtime scene exists — behaviors
        // are OPTIONAL. A motion-only or constraint-only actor (no behaviors) still
        // needs integrate_motion + the collision/terrain-constraint pipeline to run
        // so it sticks to its surface. (Mirrors game_app's job order:
        //   build_collision_frame -> [behaviors] -> integrate_motion
        //   -> apply_terrain_constraints.)
        if (!behavior_scene_) {
            return;
        }
        const bool has_behaviors = !behavior_scene_->behaviors.empty();

        // World transforms must be current before dispatch: command application
        // (set_world_translation, motion integration) reads parent world
        // matrices, and behavior transform queries read self/other world. In
        // game_app this is the compile_scene job; here we propagate directly.
        wz::scene::propagate_all(behavior_scene_->storage.polytree);

        // Build the collision frame (collision world + terrain constraint surfaces)
        // BEFORE motion/behaviors, exactly as game_app's job_build_collision_frame.
        // apply_terrain_constraints below reads frame_storage_.collision to resolve
        // the surface a terrain_constrained Motion actor sticks to. Guard the asset
        // library; if absent, the collision frame is left whatever it was (empty),
        // which makes the constraint pass a no-op rather than fabricating a surface.
        if (ctx_.assets) {
            wz::engine::collision::build_collision_frame(
                *behavior_scene_,
                ctx_.assets->collisions(),
                frame_storage_.collision);
        }
        // Proximity events (proximity.* behaviors): same per-frame build +
        // enter/stay/exit advance as game_app's job_build_collision_frame second
        // half. Needs only the scene's proximity components (no asset library).
        wz::engine::collision::build_proximity_frame(
            *behavior_scene_, frame_storage_.collision);
        // Input events (input.* behaviors, e.g. a controller-driven tank):
        // convert this frame's input into routed events so dispatch fires the
        // behavior's on_event. game_app does this in job_build_input_events; the
        // new runtime had left these tables empty, so input-driven behaviors
        // never received events.
        wz::engine::input_events::build_input_event_frame(
            input, *behavior_scene_, frame_storage_.input_events);
        std::vector<wz::scene::RuntimeEntityId> changed_entities;

        // Per-frame deferred-authoring sink: behaviors queue cheap live
        // scene-ECS authoring edits (spawn-child) here mid-dispatch; they are
        // applied below at the frame boundary, AFTER the dispatch loop finishes
        // iterating the scene (so the apply is not reentrant). The buffer is
        // function-local — runtime-owned, per-frame, never crossing a thread or
        // surviving past this tick — which is exactly the standalone-app
        // semantics #204 requires (no EditorRuntimeControl involved).
        wz::engine::behavior::BehaviorAuthoringBuffer authoring;

        // Frame-boundary spawn requests (runtime prefab spawning). A SPAWN_PREFAB
        // command is host-handled (apply_behavior_commands ignores it), like the
        // audio commands; but unlike audio it grafts nodes + rebuilds the behavior
        // runtime, so it must NOT run mid command-pass (that would renumber the
        // runtime ids the remaining commands address). It is resolved to the
        // spawner's STABLE authored id here (while behavior_scene_ is current) and
        // drained at the frame boundary, alongside the deferred-authoring edits.
        struct SpawnRequest
        {
            wz::scene::AuthoredEntityId spawner_id;
            uint32_t name_hash = 0u;
            float offset[3]{ 0.0f, 0.0f, 0.0f };
        };
        std::vector<SpawnRequest> spawn_requests;

        if (has_behaviors) {
            // Build a minimal FrameContext carrying time + input. The collision,
            // proximity and input-event tables are populated above, so the
            // dispatch routes real collision/proximity/input events to behaviors.
            wz::engine::FrameContext frame_context{};
            frame_context.input = input;
            frame_context.frame.interval.start = 0;
            frame_context.frame.interval.end = static_cast<wz::time::Tick>(
                static_cast<double>(dt)
                * static_cast<double>(
                    wz::time::TimeSource::ticks_per_second()));
            frame_context.frame.index = behavior_frame_index_++;

            frame_storage_.behavior_commands.clear();

            // Advance the monotonic sim clock the self-paced cognition scheduler
            // stamps against (the FrameContext interval is per-frame, not absolute).
            behavior_sim_time_ += static_cast<double>(dt);

            wz::engine::behavior::BehaviorFrameContext behavior_ctx{
                .frame_context = &frame_context,
                .frame_storage = &frame_storage_,
                .scene = &*behavior_scene_,
                .behavior_state = &behavior_scene_->behavior_state,
                .commands = &frame_storage_.behavior_commands,
                .gpu_compute = nullptr,
                .authoring = &authoring,
                .logger = &ctx_.logger,
                .sim_time = behavior_sim_time_,
            };
            wz::engine::behavior::dispatch_behaviors(
                *behavior_scene_, registry_, behavior_ctx);

            // Self-paced cognition: fire cognition.tick to agents whose own
            // scheduled wake is due now, APPENDING any actuator commands to the
            // same buffer (applied just below). Not a per-frame call into every
            // agent -- only those due at behavior_sim_time_.
            wz::engine::behavior::dispatch_cognition_tick(
                *behavior_scene_, registry_, behavior_ctx);

            // Apply the produced command buffer, exactly as game_app's
            // apply_behavior_commands job: transform/velocity commands mutate the
            // instance polytree, then world Y etc. settle on the next propagate.
            (void)wz::engine::behavior::apply_behavior_commands(
                *behavior_scene_,
                frame_storage_.behavior_commands.commands,
                &changed_entities);

            // Audio behavior commands (item 9): play/stop/set-gain the addressed
            // entity's AudioSource through the realtime scheduler. Only while the
            // audio runtime is live (play mode + a device); otherwise dropped (no
            // device => no sound). apply_behavior_commands ignores these kinds —
            // they don't mutate the entity, they post to the audio thread.
            if (ctx_.assets && audio_runtime_.running()) {
                namespace ea_audio = wz::engine::audio;
                for (const wz::engine::behavior::BehaviorCommand& command :
                     frame_storage_.behavior_commands.commands)
                {
                    // Grain-param control routes to a different scheduler path
                    // (it targets a grain cloud, not a voice).
                    if (command.kind == wz::engine::behavior::
                            BehaviorCommandKind::SetGrainParam) {
                        ea_audio::apply_grain_param_command(
                            *behavior_scene_, audio_runtime_.scheduler(),
                            command.entity,
                            static_cast<uint8_t>(command.values[0]),
                            command.values[1],
                            static_cast<uint32_t>(
                                command.values[2] > 0.0f ? command.values[2]
                                                         : 0.0f));
                        continue;
                    }

                    ea_audio::AudioBehaviorVerb verb;
                    switch (command.kind) {
                    case wz::engine::behavior::BehaviorCommandKind::PlaySound:
                        verb = ea_audio::AudioBehaviorVerb::Play;
                        break;
                    case wz::engine::behavior::BehaviorCommandKind::StopSound:
                        verb = ea_audio::AudioBehaviorVerb::Stop;
                        break;
                    case wz::engine::behavior::BehaviorCommandKind::SetSoundGain:
                        verb = ea_audio::AudioBehaviorVerb::SetGain;
                        break;
                    default:
                        continue;
                    }
                    ea_audio::apply_audio_behavior_command(
                        *ctx_.assets, *behavior_scene_,
                        audio_runtime_.scheduler(), verb, command.entity,
                        command.values[0], command.values[1]);
                }
            }

            // Collect SPAWN_PREFAB requests (runtime prefab spawning). Resolve the
            // spawner runtime entity -> its STABLE authored id NOW, while
            // behavior_scene_ is still the scene the command was issued against; a
            // later spawn in the drain rebuilds + renumbers the runtime, so a
            // runtime id would go stale. Decode values[0] (the name hash as a float
            // BIT PATTERN, mirroring the audio clip-name trick) and values[1..3]
            // (the offset). Drained below at the frame boundary.
            for (const wz::engine::behavior::BehaviorCommand& command :
                 frame_storage_.behavior_commands.commands)
            {
                if (command.kind
                    != wz::engine::behavior::BehaviorCommandKind::SpawnPrefab)
                {
                    continue;
                }
                if (command.entity
                    >= behavior_scene_->runtime_to_authored.size())
                {
                    continue;
                }
                uint32_t name_hash = 0u;
                std::memcpy(
                    &name_hash, &command.values[0], sizeof(uint32_t));
                spawn_requests.push_back(SpawnRequest{
                    .spawner_id =
                        behavior_scene_->runtime_to_authored[command.entity],
                    .name_hash = name_hash,
                    .offset = {
                        command.values[1],
                        command.values[2],
                        command.values[3],
                    },
                });
            }
        }

        std::vector<wz::scene::RuntimeEntityId> velocity_changed;
        (void)wz::engine::behavior::integrate_motion(
            *behavior_scene_, dt, &velocity_changed);
        changed_entities.insert(
            changed_entities.end(),
            velocity_changed.begin(),
            velocity_changed.end());

        // Snap terrain_constrained Motion actors to their constraint surface
        // (from frame_storage_.collision built above), exactly as game_app's
        // job_apply_terrain_constraints. Runs after integrate_motion so the
        // constraint corrects the just-integrated position.
        std::vector<wz::scene::RuntimeEntityId> constraint_changed;
        (void)wz::engine::behavior::apply_terrain_constraints(
            *behavior_scene_,
            frame_storage_.collision,
            dt,
            &constraint_changed);
        changed_entities.insert(
            changed_entities.end(),
            constraint_changed.begin(),
            constraint_changed.end());

        std::sort(changed_entities.begin(), changed_entities.end());
        changed_entities.erase(
            std::unique(changed_entities.begin(), changed_entities.end()),
            changed_entities.end());

        if (!changed_entities.empty()) {
            // Re-propagate so world matrices (and any next-frame world-space
            // reads) reflect the applied local changes. #221: the polytree is now
            // the single source of truth for render/audio/spawn world transforms
            // (see scene_world_transforms()), so there is no per-frame write-back
            // into scene_nodes_ any more -- scene_nodes_ transforms are derived
            // from the polytree only when actually needed (save + editor
            // read-back). The lossy Mat4->TRS->Mat4 round trip is gone from the
            // frame path.
            wz::scene::propagate_all(behavior_scene_->storage.polytree);
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

        // reparent drain: same frame-boundary, same apply method the host's
        // reparent uses (reparent_node, which rebuilds the behavior runtime on
        // success like add_child/remove). Both ids were resolved to authored
        // ids at enqueue time (an empty new_parent_id = top level), so they stay
        // valid even as a prior structural drain entry renumbers runtime ids.
        for (const wz::engine::behavior::BehaviorReparentRequest& request :
             authoring.reparent_requests)
        {
            if (!reparent_node(request.node_id, request.new_parent_id)) {
                ctx_.logger.warn(
                    "behavior reparent_node rejected for node '"
                    + request.node_id + "'");
            }
        }

        // add/remove-component drains: same frame-boundary, same apply methods
        // the host uses (add_node_component / remove_node_component). These are
        // cheap field edits (no behavior-runtime rebuild, no asset-DAG
        // recompile), so they are order-independent of the structural drains
        // above. The kind was copied into each request at enqueue time (the
        // behavior's transient const char* is long gone by now).
        for (const wz::engine::behavior::BehaviorComponentRequest& request :
             authoring.add_component_requests)
        {
            if (!add_node_component(request.node_id, request.kind)) {
                ctx_.logger.warn(
                    "behavior add_node_component rejected for node '"
                    + request.node_id + "' kind '" + request.kind + "'");
            }
        }
        for (const wz::engine::behavior::BehaviorComponentRequest& request :
             authoring.remove_component_requests)
        {
            if (!remove_node_component(request.node_id, request.kind)) {
                ctx_.logger.warn(
                    "behavior remove_node_component rejected for node '"
                    + request.node_id + "' kind '" + request.kind + "'");
            }
        }

        // SPAWN_PREFAB drain (runtime prefab spawning): same frame-boundary as the
        // deferred-authoring edits above, AFTER every read of behavior_scene_ this
        // tick — each spawn grafts nodes + rebuilds the behavior runtime out from
        // under us, so it is safe only now. The spawner is addressed by its stable
        // authored id (resolved during the command pass), so it stays valid even as
        // a prior spawn here rebuilds + renumbers the runtime. State preservation
        // in rebuild_behavior_scene leaves pre-existing bindings' state untouched.
        for (const SpawnRequest& request : spawn_requests) {
            spawn_prefab(
                request.spawner_id,
                request.name_hash,
                request.offset[0],
                request.offset[1],
                request.offset[2]);
        }
    }

    std::size_t WozzitsApp_v1::active_behavior_binding_count() const
    {
        return behavior_scene_ ? behavior_scene_->behaviors.size() : 0u;
    }

    std::optional<wz::engine::assets::AuthoredTransform>
    WozzitsApp_v1::derived_authored_transform(
        const wz::scene::AuthoredEntityId& id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, id);
        if (!node) {
            return std::nullopt;
        }

        // Default: the node's own stored authored transform.
        wz::engine::assets::AuthoredTransform out = node->local;

        // With a live behavior scene, derive from the polytree's LOCAL matrix so
        // sim-driven movement is reported/saved -- the #221 replacement for the
        // old per-frame write-back. A node missing from the runtime map, or a
        // matrix decompose_trs rejects, keeps the stored authored transform.
        if (behavior_scene_) {
            const auto it = behavior_scene_->authored_to_runtime.find(id);
            if (it != behavior_scene_->authored_to_runtime.end()
                && it->second < wz::core::graph::node_count(
                       behavior_scene_->storage.polytree)) {
                const wz::math::Mat4& local = wz::core::graph::node_data(
                    behavior_scene_->storage.polytree, it->second).local;
                (void)authored_transform_from_local(local, out);
            }
        }
        return out;
    }

    std::optional<wz::math::Vec3> WozzitsApp_v1::node_local_translation(
        const wz::scene::AuthoredEntityId& id) const
    {
        const std::optional<wz::engine::assets::AuthoredTransform> local =
            derived_authored_transform(id);
        if (!local) {
            return std::nullopt;
        }
        return wz::math::Vec3{
            local->translation[0],
            local->translation[1],
            local->translation[2],
        };
    }

    std::optional<wz::math::Vec3> WozzitsApp_v1::node_local_scale(
        const wz::scene::AuthoredEntityId& id) const
    {
        const std::optional<wz::engine::assets::AuthoredTransform> local =
            derived_authored_transform(id);
        if (!local) {
            return std::nullopt;
        }
        return wz::math::Vec3{
            local->scale[0],
            local->scale[1],
            local->scale[2],
        };
    }

    std::optional<wz::math::Vec3> WozzitsApp_v1::stored_node_local_translation(
        const wz::scene::AuthoredEntityId& id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, id);
        if (!node) {
            return std::nullopt;
        }
        // The raw stored authored transform — deliberately NOT derived from the
        // sim polytree (#221), so a test can prove scene_nodes_ stays put while
        // the sim moves the node.
        return wz::math::Vec3{
            node->local.translation[0],
            node->local.translation[1],
            node->local.translation[2],
        };
    }

    void WozzitsApp_v1::apply_node_local_transform(
        const wz::scene::AuthoredEntityId& id,
        const wz::engine::assets::AuthoredTransform& transform)
    {
        // #221: the ONE place a transform edit lands. The polytree is the single
        // source of truth for the drawn + saved pose (scene_world_transforms /
        // derived_authored_transform read it, and rebuild_behavior_scene restores
        // a surviving node's live local by authored id), so write the LOCAL there
        // and do NOT touch scene_nodes_'s stored transform. For a sim-driven node
        // (e.g. a terrain_constrained actor) the next tick's propagate_all settles
        // world matrices and the scale-preserving constraint keeps the authored
        // scale — an edit is no longer reverted by any per-frame write-back
        // (there is none now). The polytree always exists for a loaded scene, so
        // this is the taken path.
        wz::math::Transform trs{};
        trs.position = {
            transform.translation[0],
            transform.translation[1],
            transform.translation[2],
        };
        trs.rotation = {
            transform.rotation_quat[0],
            transform.rotation_quat[1],
            transform.rotation_quat[2],
            transform.rotation_quat[3],
        };
        trs.scale = {
            transform.scale[0],
            transform.scale[1],
            transform.scale[2],
        };

        if (behavior_scene_) {
            const auto it = behavior_scene_->authored_to_runtime.find(id);
            if (it != behavior_scene_->authored_to_runtime.end()
                && it->second < wz::core::graph::node_count(
                       behavior_scene_->storage.polytree)) {
                // node_data is const-only; the constraint pipeline / behavior
                // command apply mutate the polytree the same way (const_cast).
                const_cast<wz::scene::TransformNode&>(
                    wz::core::graph::node_data(
                        behavior_scene_->storage.polytree, it->second))
                    .local = wz::math::transform(trs);
                // Re-propagate so the world matrices reflect the edited local
                // immediately: the editor renders (scene_world_transforms reads
                // node_data().world) without necessarily ticking the sim between
                // an edit and the next draw, so the edit must be visible now, not
                // only after the next simulation_tick's propagate_all. Cheap for a
                // single edit; a sim-driven node re-settles on the next tick.
                wz::scene::propagate_all(behavior_scene_->storage.polytree);
                scene_dirty_ = true;
                return;
            }
        }

        // DEGENERATE fallback: no live polytree entry for this node (a failed
        // instantiate_scene left behavior_scene_ null, or the node is absent from
        // the runtime map). scene_world_transforms / derived_authored_transform
        // fall back to scene_nodes_ in exactly that case, so write the stored
        // transform here so the edit still takes effect and the editor recovers.
        if (wz::engine::assets::SceneNodeAsset* node =
                wz::engine::assets::find_scene_node(scene_nodes_, id)) {
            wz::engine::assets::set_transform(*node, transform);
            scene_dirty_ = true;
        }
    }

    bool WozzitsApp_v1::set_node_transform(
        const wz::scene::AuthoredEntityId& id,
        const wz::engine::assets::AuthoredTransform& transform)
    {
        // Resolve existence, then route through the single #221 edit seam. The
        // scene_nodes_ transform write is gone from the edit path: Phase 1's
        // derivation covers persistence (save_scene + authored_scene_nodes derive
        // from the polytree) and rebuilds restore live locals via the
        // preservation map, so the polytree write the seam does is sufficient.
        if (!wz::engine::assets::find_scene_node(scene_nodes_, id)) {
            return false;
        }
        apply_node_local_transform(id, transform);
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

    bool WozzitsApp_v1::set_node_audio_renderable(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        const bool ok = wz::engine::assets::set_node_audio_renderable(
            scene_nodes_, node_id, asset_graph_node_id);
        if (ok) {
            scene_dirty_ = true;
            // The node id resolves to the audio_renderable key in
            // assemble_render_bindings on the next bind; nothing to draw, so no
            // immediate rematerialize is needed (mirrors set_node_renderable_asset).
        }
        else {
            ctx_.logger.warn(
                "set_node_audio_renderable: no-op (node '" + node_id
                + "' missing)");
        }
        return ok;
    }

    bool WozzitsApp_v1::set_node_audio_source_play(
        const wz::scene::AuthoredEntityId& node_id,
        bool auto_play,
        bool enabled)
    {
        const bool ok = wz::engine::assets::set_node_audio_source_play(
            scene_nodes_, node_id, auto_play, enabled);
        if (ok) {
            scene_dirty_ = true;
        }
        else {
            ctx_.logger.warn(
                "set_node_audio_source_play: no-op (node '" + node_id
                + "' missing or has no AudioSource)");
        }
        return ok;
    }

    bool WozzitsApp_v1::set_node_scene_source(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        const bool ok = wz::engine::assets::set_node_scene_source(
            scene_nodes_, node_id, asset_graph_node_id);
        if (!ok) {
            ctx_.logger.warn(
                "set_node_scene_source: no-op (node '" + node_id + "' missing)");
            return false;
        }
        scene_dirty_ = true;
        // Re-resolve the new (or absent) scene source against the bound graph,
        // then re-graft so the host's children reflect the change immediately,
        // and re-materialize the behavior runtime (the grafted children change
        // the entity set). bridge_scene_source_keys clears the key when the
        // authored node id was cleared, so graft_scene_sources drops the stale
        // graft and adds nothing — the children disappear, as intended.
        wz::engine::assets::bridge_scene_source_keys(scene_nodes_, graph_draft_);
        graft_scene_sources();
        // The grafted children carry intrinsic GLB-part geometry bindings (#213
        // increment 3); assemble them into renderables (inheriting the host's
        // program) so the subtree draws.
        rematerialize_render_bindings();
        rebuild_behavior_scene();
        return true;
    }

    bool WozzitsApp_v1::set_node_geometry_asset(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_geometry_asset: no-op (node '" + node_id
                + "' missing)");
            return false;
        }
        if (asset_graph_node_id != 0) {
            wz::engine::assets::attach_geometry_asset_node(
                *node, asset_graph_node_id);
        }
        else {
            wz::engine::assets::detach_geometry_asset_node(*node);
            // No geometry => the binding no longer drives this node; drop the
            // renderable it assembled so it stops drawing (the re-bridge in
            // rematerialize restores a pre-built renderable_asset_node_id if any).
            node->renderable_asset.reset();
        }
        scene_dirty_ = true;
        rematerialize_render_bindings();
        return true;
    }

    bool WozzitsApp_v1::set_node_render_program(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_render_program: no-op (node '" + node_id
                + "' missing)");
            return false;
        }
        if (asset_graph_node_id != 0) {
            wz::engine::assets::attach_render_program_node(
                *node, asset_graph_node_id);
        }
        else {
            wz::engine::assets::detach_render_program_node(*node);
        }
        scene_dirty_ = true;
        // If this targets a runtime-only grafted scene-source child, mirror the
        // program onto its host as a sticky override (issue #213) so it survives
        // reload (save_scene excludes grafted children). No-op for authored nodes.
        capture_grafted_child_override(node_id);
        // A program change cascades to descendants via inheritance, so
        // re-assemble every binding (assemble walks ancestors per node).
        rematerialize_render_bindings();
        return true;
    }

    bool WozzitsApp_v1::set_node_collision_asset(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id,
        bool constrain_movement)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_collision_asset: no-op (node '" + node_id
                + "' missing)");
            return false;
        }
        if (asset_graph_node_id != 0) {
            wz::engine::assets::attach_collision_asset_node(
                *node, asset_graph_node_id, constrain_movement);
        }
        else {
            wz::engine::assets::detach_collision_asset_node(*node);
            // A cleared reference also clears constrain_movement so the node
            // stops constraining (it has no surface to stick to).
            if (node->collision) {
                node->collision->constrain_movement = constrain_movement;
            }
        }
        scene_dirty_ = true;
        // Re-point the (possibly new) reference at the bound graph's collision
        // key, then re-materialize so the runtime scene picks up the constraint
        // surface (rebuild_behavior_scene rebuilds the SceneInstance whose
        // collision world the constraint loop reads).
        wz::engine::assets::bridge_scene_collision_keys(
            scene_nodes_, graph_draft_);
        rebuild_behavior_scene();
        return true;
    }

    bool WozzitsApp_v1::set_node_motion_terrain_fields(
        const wz::scene::AuthoredEntityId& node_id,
        bool terrain_constrained,
        float ride_height,
        float footprint_radius,
        bool align_to_surface,
        float alignment_strength)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_motion_terrain_fields: no-op (node '" + node_id
                + "' missing)");
            return false;
        }
        const bool adding_component = !node->motion.has_value();
        if (adding_component) {
            node->motion.emplace();
        }
        node->motion->terrain_constrained = terrain_constrained;
        node->motion->terrain_ride_height = ride_height;
        node->motion->terrain_footprint_radius = footprint_radius;
        node->motion->terrain_align_to_surface = align_to_surface;
        node->motion->terrain_alignment_strength = alignment_strength;
        scene_dirty_ = true;

        // #221: patch the LIVE Motion record in place instead of rebuilding the
        // whole runtime for a field tweak (a full rebuild_behavior_scene would
        // reset behavior/sim state — and snap sim-driven actors, which the
        // preservation map then has to restore — for what is just a component
        // field change). Only ADDING the Motion component (no live record yet)
        // needs a rebuild so the record is materialized. Crucially, the in-place
        // path leaves terrain_alignment_rate ALONE — it is a runtime-only field a
        // behavior set (SetTerrainAlignmentRate) that does NOT live on the
        // authored asset, and it rides the SAME MotionComponent record; wiping it
        // here would reset a self.start-configured actor to instant alignment.
        if (!adding_component && behavior_scene_) {
            const auto it = behavior_scene_->authored_to_runtime.find(node_id);
            if (it != behavior_scene_->authored_to_runtime.end()) {
                for (auto& record : behavior_scene_->motions) {
                    if (record.node != it->second) {
                        continue;
                    }
                    // Only the authored terrain-stick fields; terrain_alignment_-
                    // rate (runtime-only) and velocities are untouched.
                    record.component.terrain_constrained = terrain_constrained;
                    record.component.terrain_ride_height = ride_height;
                    record.component.terrain_footprint_radius = footprint_radius;
                    record.component.terrain_align_to_surface = align_to_surface;
                    record.component.terrain_alignment_strength =
                        alignment_strength;
                    return true;
                }
            }
        }

        // Adding the component (or, defensively, no matching live record) needs a
        // rebuild so the Motion record participates in integrate_motion +
        // apply_terrain_constraints.
        rebuild_behavior_scene();
        return true;
    }

    void WozzitsApp_v1::rematerialize_render_bindings()
    {
        if (!ctx_.assets) {
            return;
        }
        // Re-bridge the pre-built renderables first (so a node that lost its
        // geometry can fall back to renderable_asset_node_id if it has one), then
        // re-assemble the ingredient bindings (which override for geometry nodes).
        wz::engine::assets::bridge_scene_renderable_keys(
            scene_nodes_, graph_draft_);
        const std::size_t assembled = assemble_render_bindings(graph_draft_);
        if (assembled > 0) {
            ctx_.assets->commit();
            const wz::engine::assets::ResolveReport resolve =
                ctx_.assets->resolve_all();
            if (!resolve.ok()) {
                ctx_.logger.warn(
                    "rematerialize_render_bindings: resolved with errors="
                    + std::to_string(resolve.failures.size()));
            }
        }
    }

    bool WozzitsApp_v1::set_node_glb_scene_source(
        const wz::scene::AuthoredEntityId& node_id,
        const wz::engine::assets::SceneGLBSceneSource& descriptor)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_glb_scene_source: no-op (node '" + node_id
                + "' missing)");
            return false;
        }

        // An empty path clears the descriptor (and any node-ref scene source);
        // otherwise attach the authored descriptor. attach_glb_scene_source also
        // clears the asset-graph-node route so the node stays single-route.
        if (descriptor.path.empty()) {
            wz::engine::assets::detach_scene_source(*node);
        }
        else {
            wz::engine::assets::attach_glb_scene_source(*node, descriptor);
        }
        scene_dirty_ = true;

        // Re-materialize so the change shows on the next frame. Mirror the
        // descriptor-route sequence load_scene runs (NOT the node-ref bridge that
        // set_node_scene_source uses): re-resolve the descriptor into a Scene
        // asset, compile the freshly registered assets, then re-graft + rebuild.
        rematerialize_glb_scene_sources();
        return true;
    }

    void WozzitsApp_v1::rematerialize_glb_scene_sources()
    {
        // Guarded by the asset library (resolve/graft are no-ops without it).
        if (!ctx_.assets) {
            return;
        }

        const std::size_t resolved = resolve_glb_scene_sources();
        if (resolved > 0) {
            ctx_.assets->commit();
            const wz::engine::assets::ResolveReport glb_resolve =
                ctx_.assets->resolve_all();
            if (!glb_resolve.ok()) {
                ctx_.logger.warn(
                    "rematerialize_glb_scene_sources: GLB scene-source resolved "
                    "with errors=" + std::to_string(glb_resolve.failures.size()));
            }
        }
        // Re-graft so the hosts' children reflect the change immediately (a
        // cleared descriptor leaves no scene_source, so graft drops the stale
        // children and adds nothing), then rebuild the behavior runtime since the
        // grafted children change the addressable entity set.
        graft_scene_sources();
        // Assemble the grafted children's intrinsic geometry bindings (#213
        // increment 3) so they draw under the host's inherited program.
        rematerialize_render_bindings();
        rebuild_behavior_scene();
    }

    // ─── Per-component GLB render-style authoring (issue #213 Phase 3b-2) ──────
    // Each style mutator edits the styling inside the node's glb_scene_source
    // DESCRIPTOR (the persisted authored data), then runs the shared 3a re-
    // materialize so the re-keyed Scene rebuilds with the new per-mesh look. The
    // descriptor's base_style/style_overrides fold into create_scene_from_glb's
    // content key, so a style change yields a different key => a rebuilt scene.
    // Fail closed (logged no-op + false) when the node has no glb_scene_source.

    bool WozzitsApp_v1::set_node_glb_base_style(
        const wz::scene::AuthoredEntityId& node_id,
        const wz::engine::assets::MeshRenderStyleData& style)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node || !node->glb_scene_source) {
            ctx_.logger.warn(
                "set_node_glb_base_style: no-op (node '" + node_id
                + "' has no GLB scene source)");
            return false;
        }

        node->glb_scene_source->base_style = style;
        scene_dirty_ = true;
        rematerialize_glb_scene_sources();
        return true;
    }

    bool WozzitsApp_v1::set_node_glb_mesh_style(
        const wz::scene::AuthoredEntityId& node_id,
        uint32_t mesh_index,
        const wz::engine::assets::MeshRenderStyleData& style)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node || !node->glb_scene_source) {
            ctx_.logger.warn(
                "set_node_glb_mesh_style: no-op (node '" + node_id
                + "' has no GLB scene source)");
            return false;
        }

        // Replace-or-insert the override for this mesh index.
        std::vector<wz::engine::assets::SceneGLBSceneSourceStyleOverride>&
            overrides = node->glb_scene_source->style_overrides;
        const auto it = std::find_if(
            overrides.begin(),
            overrides.end(),
            [mesh_index](
                const wz::engine::assets::SceneGLBSceneSourceStyleOverride& ov) {
                return ov.mesh_index == mesh_index;
            });
        if (it != overrides.end()) {
            it->style = style;
        }
        else {
            overrides.push_back(
                wz::engine::assets::SceneGLBSceneSourceStyleOverride{
                    .mesh_index = mesh_index,
                    .style = style,
                });
        }

        scene_dirty_ = true;
        rematerialize_glb_scene_sources();
        return true;
    }

    bool WozzitsApp_v1::clear_node_glb_mesh_style(
        const wz::scene::AuthoredEntityId& node_id,
        uint32_t mesh_index)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node || !node->glb_scene_source) {
            ctx_.logger.warn(
                "clear_node_glb_mesh_style: no-op (node '" + node_id
                + "' has no GLB scene source)");
            return false;
        }

        std::vector<wz::engine::assets::SceneGLBSceneSourceStyleOverride>&
            overrides = node->glb_scene_source->style_overrides;
        const auto before = overrides.size();
        overrides.erase(
            std::remove_if(
                overrides.begin(),
                overrides.end(),
                [mesh_index](
                    const wz::engine::assets::SceneGLBSceneSourceStyleOverride&
                        ov) { return ov.mesh_index == mesh_index; }),
            overrides.end());

        // Even a no-op clear (no such override) returns true: the requested state
        // — "no override for this mesh" — holds. Only re-materialize + mark dirty
        // when something actually changed.
        if (overrides.size() != before) {
            scene_dirty_ = true;
            rematerialize_glb_scene_sources();
        }
        return true;
    }

    bool WozzitsApp_v1::flatten_scene_source(
        const wz::scene::AuthoredEntityId& node_id)
    {
        if (!ctx_.assets) {
            ctx_.logger.warn("flatten_scene_source: no asset library");
            return false;
        }

        wz::engine::assets::SceneNodeAsset* host =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!host) {
            ctx_.logger.warn(
                "flatten_scene_source: no-op (node '" + node_id + "' missing)");
            return false;
        }

        // Resolve the host's scene source: prefer the cached resolved key, else
        // bridge from the authored node id against the bound graph.
        if (!host->scene_source && host->scene_source_node_id) {
            wz::engine::assets::bridge_scene_source_keys(
                scene_nodes_, graph_draft_);
            host = wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        }
        if (!host || !host->scene_source) {
            ctx_.logger.warn(
                "flatten_scene_source: node '" + node_id
                + "' has no resolvable scene source");
            return false;
        }

        const wz::engine::assets::SceneHandle handle =
            ctx_.assets->scenes().get_scene(
                wz::engine::assets::SceneAsset{ .output = *host->scene_source });
        const wz::engine::assets::SceneAssetData* sub =
            ctx_.assets->scenes().get_scene_data(handle);
        if (!sub) {
            ctx_.logger.warn(
                "flatten_scene_source: scene source for node '" + node_id
                + "' did not resolve to a Scene asset");
            return false;
        }

        // If this host currently has an INSTANCE graft live, drop those runtime
        // children first so flatten does not duplicate them (the persistent
        // expansion below replaces them as authored nodes with the same ids).
        const std::string prefix = node_id + "/";
        if (!grafted_node_ids_.empty()) {
            std::unordered_set<std::string> stale;
            for (const auto& id : grafted_node_ids_) {
                if (id.rfind(prefix, 0) == 0) {
                    stale.insert(id);
                }
            }
            if (!stale.empty()) {
                scene_nodes_.erase(
                    std::remove_if(
                        scene_nodes_.begin(),
                        scene_nodes_.end(),
                        [&stale](
                            const wz::engine::assets::SceneNodeAsset& n) {
                            return stale.count(n.id) != 0;
                        }),
                    scene_nodes_.end());
                grafted_node_ids_.erase(
                    std::remove_if(
                        grafted_node_ids_.begin(),
                        grafted_node_ids_.end(),
                        [&stale](const wz::scene::AuthoredEntityId& id) {
                            return stale.count(id) != 0;
                        }),
                    grafted_node_ids_.end());
            }
            host = wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        }

        // Expand persistently: the children become real authored nodes (NOT
        // tracked in grafted_node_ids_, so they persist + save), and the host's
        // scene-source reference is dropped — the expansion is now the content.
        // Snapshot the host BEFORE the appends (push_back may reallocate
        // scene_nodes_ and invalidate `host`), then re-find it to detach.
        const wz::engine::assets::SceneNodeAsset host_snapshot = *host;
        std::vector<wz::engine::assets::SceneNodeAsset> children =
            wz::engine::assets::expand_scene_source_children(host_snapshot, *sub);
        const std::size_t count = children.size();
        for (wz::engine::assets::SceneNodeAsset& child : children) {
            scene_nodes_.push_back(std::move(child));
        }
        host = wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (host) {
            wz::engine::assets::detach_scene_source(*host);
        }

        scene_dirty_ = true;
        rebuild_behavior_scene();
        ctx_.logger.info(
            "flatten_scene_source: expanded " + std::to_string(count)
            + " node(s) under '" + node_id + "' (scene source dropped)");
        return true;
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

    std::optional<wz::asset::AssetGraphDraftNodeId>
    WozzitsApp_v1::node_scene_source_node_id(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node) {
            return std::nullopt;
        }
        return node->scene_source_node_id;
    }

    bool WozzitsApp_v1::node_has_glb_scene_source(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        return node && node->glb_scene_source.has_value();
    }

    const wz::engine::assets::SceneGLBSceneSource*
    WozzitsApp_v1::node_glb_scene_source(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node || !node->glb_scene_source) {
            return nullptr;
        }
        return &*node->glb_scene_source;
    }

    const wz::engine::assets::SceneCollisionAsset*
    WozzitsApp_v1::node_collision(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node || !node->collision) {
            return nullptr;
        }
        return &*node->collision;
    }

    const wz::engine::assets::SceneMotionAsset*
    WozzitsApp_v1::node_motion(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
        if (!node || !node->motion) {
            return nullptr;
        }
        return &*node->motion;
    }

    std::vector<wz::engine::assets::SceneNodeAsset>
    WozzitsApp_v1::grafted_scene_nodes() const
    {
        if (grafted_node_ids_.empty()) {
            return {};
        }

        const std::unordered_set<std::string> grafted(
            grafted_node_ids_.begin(), grafted_node_ids_.end());
        std::vector<wz::engine::assets::SceneNodeAsset> out;
        out.reserve(grafted.size());
        for (const wz::engine::assets::SceneNodeAsset& node : scene_nodes_) {
            if (grafted.count(node.id) != 0) {
                out.push_back(node);  // copy: the seam hands a snapshot back
            }
        }
        return out;
    }

    std::vector<wz::engine::assets::SceneNodeAsset>
    WozzitsApp_v1::authored_scene_nodes() const
    {
        // Same filter as save_scene: drop runtime-only grafted (#213) + "spawn:"
        // prefab-instance nodes, leaving the authored scene the editor edits.
        const std::unordered_set<std::string> grafted(
            grafted_node_ids_.begin(), grafted_node_ids_.end());
        std::vector<wz::engine::assets::SceneNodeAsset> authored;
        authored.reserve(scene_nodes_.size());
        for (const wz::engine::assets::SceneNodeAsset& n : scene_nodes_) {
            if (grafted.count(n.id) != 0 || n.id.rfind("spawn:", 0) == 0) {
                continue;
            }
            authored.push_back(n);
            // #221: report the sim-current pose. With a live behavior scene the
            // node's transform is derived from the polytree (the old write-back
            // used to keep scene_nodes_ current); with no sim this is the stored
            // authored transform unchanged.
            if (const std::optional<wz::engine::assets::AuthoredTransform>
                    derived = derived_authored_transform(n.id)) {
                authored.back().local = *derived;
            }
        }
        return authored;
    }

    bool WozzitsApp_v1::open_scene(const wz::fs::Path& scene_path)
    {
        if (asset_graph_path_.empty()) {
            ctx_.logger.error(
                "open_scene: no scene loaded yet (no asset graph to reuse)");
            return false;
        }
        // An empty path means "reopen the project's main scene" (switch back from a
        // scenelet). Otherwise open the named scene.
        const wz::fs::Path target =
            scene_path.empty() ? main_scene_path_ : scene_path;
        if (target.empty()) {
            ctx_.logger.error("open_scene: no scene to open");
            return false;
        }

        ctx_.logger.info(
            "open_scene: swapping working scene to '" + target + "'");
        // Reuse the current project asset graph + module folder; only the scene
        // changes. load_scene re-binds the graph (a v2 optimization can skip that
        // when unchanged), materializes the new scene, and rebuilds the behavior
        // runtime -- so the editor's next snapshot sees the scenelet's nodes.
        return load_scene(WozzitsAppSceneLoadDesc{
            .asset_graph = asset_graph_path_,
            .scene = target,
            .behavior_module_folder = behavior_module_folder_,
        });
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

        // Exclude runtime-only nodes from the saved scene: GLB-grafted scene-
        // source children (#213, re-imported from the reference at load) AND
        // runtime-spawned prefab instances (their ids carry a "spawn:" prefix,
        // covering both the spawned roots and their own grafted children). Both
        // are ephemeral runtime state -- persisting them would re-load as authored
        // nodes and collide with a fresh spawn's ids. Flattened nodes are authored
        // (not tracked here), so they persist normally.
        std::vector<wz::engine::assets::SceneNodeAsset> persisted_nodes;
        {
            const std::unordered_set<std::string> grafted(
                grafted_node_ids_.begin(), grafted_node_ids_.end());
            persisted_nodes.reserve(scene_nodes_.size());
            for (const wz::engine::assets::SceneNodeAsset& n : scene_nodes_) {
                if (grafted.count(n.id) != 0
                    || n.id.rfind("spawn:", 0) == 0) {
                    continue;
                }
                persisted_nodes.push_back(n);
                // #221: derive-on-save. The per-frame write-back that used to keep
                // scene_nodes_ transforms sim-current is gone, so decompose the
                // live polytree pose into the saved node here (one lossy decompose
                // at save, not per frame). No sim => the stored authored transform.
                if (const std::optional<wz::engine::assets::AuthoredTransform>
                        derived = derived_authored_transform(n.id)) {
                    persisted_nodes.back().local = *derived;
                }
            }
        }

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
            wz::engine::assets::set_scene_document_nodes(
                document, persisted_nodes);
        }
        else {
            // No readable source — emit a fresh scene document from the nodes.
            wz::engine::assets::SceneAssetData snapshot;
            snapshot.nodes = persisted_nodes;
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

    bool WozzitsApp_v1::export_subtree_as_scene(
        std::string_view root_node_id,
        const wz::fs::Path& out_path)
    {
        // Carve the subtree out of the LIVE nodes. A missing root id yields no
        // subtree — bail (the caller treats it as a failure).
        std::optional<wz::engine::assets::SceneAssetData> subtree =
            wz::engine::assets::extract_scene_subtree(
                scene_nodes_, root_node_id);
        if (!subtree) {
            ctx_.logger.error(
                "export_subtree_as_scene: root node not found");
            return false;
        }

        // Exclude runtime-grafted instance children (#213), mirroring
        // save_scene: a prefab keeps a scene_source host's reference but not the
        // sub-tree it grafts at load (that re-imports from the reference). The
        // host node itself (with its scene_source) stays if it is in the subtree.
        if (!grafted_node_ids_.empty()) {
            const std::unordered_set<std::string> grafted(
                grafted_node_ids_.begin(), grafted_node_ids_.end());
            std::vector<wz::engine::assets::SceneNodeAsset> kept;
            kept.reserve(subtree->nodes.size());
            for (wz::engine::assets::SceneNodeAsset& n : subtree->nodes) {
                if (grafted.count(n.id) == 0) {
                    kept.push_back(std::move(n));
                }
            }
            subtree->nodes = std::move(kept);
        }

        // Resolve the output path the way save_scene resolves its source path:
        // an absolute path is used as-is, otherwise it joins the resource root.
        const wz::fs::Path resource_root =
            ctx_.assets ? ctx_.assets->resource_root() : wz::fs::Path{};
        const wz::fs::Path path =
            wz::fs::is_absolute(out_path) || resource_root.empty()
                ? out_path
                : wz::fs::join(resource_root, out_path);

        // Emit a FRESH scene document: a prefab is self-contained, so there is no
        // existing-file non-node data to preserve (unlike save_scene).
        wz::json::JSONDocument document =
            wz::engine::assets::export_scene_to_json_document(*subtree);

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            ctx_.logger.error(
                "export_subtree_as_scene: could not open output file");
            return false;
        }
        out << wz::json::serialize_json(document);
        if (!out.good()) {
            ctx_.logger.error(
                "export_subtree_as_scene: write failed");
            return false;
        }

        ctx_.logger.info("export_subtree_as_scene: prefab written");
        return true;
    }

    std::vector<wz::math::Mat4> WozzitsApp_v1::scene_world_transforms() const
    {
        // No live simulation: the authored composition IS the truth (nothing
        // moves scene_nodes_'s transforms), so compose from the nodes exactly as
        // the renderer used to.
        if (!behavior_scene_) {
            return wz::engine::rendering::compute_scene_node_world_transforms(
                scene_nodes_);
        }

        // Live simulation: the polytree carries the composed world matrices the
        // sim advanced this frame (propagate_all runs in simulation_tick). Read
        // each node's world straight from it, mapped by stable authored id. Seed
        // from the authored composition so any node missing from the runtime map
        // (defensive — the instance mirrors scene_nodes_ 1:1, so this should not
        // happen) still gets a sensible transform rather than identity.
        std::vector<wz::math::Mat4> world =
            wz::engine::rendering::compute_scene_node_world_transforms(
                scene_nodes_);
        const std::size_t node_count =
            wz::core::graph::node_count(behavior_scene_->storage.polytree);
        for (std::size_t i = 0; i < scene_nodes_.size(); ++i) {
            const auto it =
                behavior_scene_->authored_to_runtime.find(scene_nodes_[i].id);
            if (it == behavior_scene_->authored_to_runtime.end()
                || it->second >= node_count) {
                continue;
            }
            world[i] = wz::core::graph::node_data(
                behavior_scene_->storage.polytree, it->second).world;
        }
        return world;
    }

    std::optional<wz::math::Mat4> WozzitsApp_v1::node_world_transform(
        const wz::scene::AuthoredEntityId& id) const
    {
        for (std::size_t i = 0; i < scene_nodes_.size(); ++i) {
            if (scene_nodes_[i].id == id) {
                return scene_world_transforms()[i];
            }
        }
        return std::nullopt;
    }

    bool WozzitsApp_v1::render_scene()
    {
        if (!ctx_.assets) {
            return true;
        }
        // The single active view is kept current by update_active_view() each
        // simulation_tick. render_scene just reads it -- no branch, no scene-tree
        // lookup, no fallback. world_position drives the clipmap lattice snap and
        // tracks whichever camera (free-fly or scene) is active.
        //
        // #221: hand the renderer the world transforms from the single source of
        // truth (the live polytree when simulating, the authored composition
        // otherwise) so the drawn pose is the sim-current one -- the per-frame
        // Mat4->TRS->Mat4 write-back into scene_nodes_ is gone.
        const std::vector<wz::math::Mat4> world_transforms =
            scene_world_transforms();
        return renderer_.render_scene(
            scene_nodes_, *ctx_.assets, active_view_.view_projection,
            active_view_.world_position, world_transforms);
    }

    void WozzitsApp_v1::set_prefer_scene_camera(bool prefer)
    {
        prefer_scene_camera_ = prefer;

        // Leaving play silences the runtime; entering play defers device start to
        // the next scene load (start_scene_audio).
        if (!prefer) {
            audio_runtime_.stop();
        }
    }

    void WozzitsApp_v1::start_scene_audio()
    {
        // Editor stays silent (audition is a later editor path); only play mode
        // auto-plays. Audio is optional — a missing device must not fail the app.
        if (!prefer_scene_camera_)
            return;
        if (!ctx_.assets || !behavior_scene_)
            return;

        if (!audio_runtime_.running()) {
            if (!audio_runtime_.start()) {
                ctx_.logger.warn(
                    "audio device unavailable; scene audio disabled");
                return;
            }
        }

        // Drop any descs from a prior scene (the runtime was stopped on scene
        // exit, so the audio thread no longer references them), then repopulate.
        grain_desc_store_.clear();

        // Reset spatialization velocity tracking so a new scene starts with no
        // stale prev positions (the first tick then skips Doppler).
        audio_spatialization_.clear();

        const wz::engine::audio::ScenePlaybackReport report =
            wz::engine::audio::play_scene_audio_sources(
                *ctx_.assets, *behavior_scene_, audio_runtime_.scheduler(),
                grain_desc_store_);

        ctx_.logger.info(
            "scene audio: played " + std::to_string(report.played)
            + " source(s), skipped " + std::to_string(report.skipped_disabled)
            + " disabled / " + std::to_string(report.skipped_unresolved)
            + " unresolved");
    }

    void WozzitsApp_v1::update_active_view()
    {
        if (camera_source_ == CameraSource::Scene) {
            // Read the selected camera node's already-maintained world transform
            // straight from the live scene graph through its handle. A camera
            // parented under a moving node (e.g. a tank) follows it because the
            // graph keeps that node's world matrix current. If the handle can't
            // be resolved this frame (e.g. mid-rebuild), keep the previous active
            // view rather than dropping to free-fly -- so a transient invalid
            // handle cannot flip the camera.
            const std::size_t node_count = behavior_scene_
                ? wz::core::graph::node_count(behavior_scene_->storage.polytree)
                : 0;
            const bool resolved = behavior_scene_
                && active_camera_entity_ != wz::scene::INVALID_RUNTIME_ENTITY
                && active_camera_entity_ < node_count;

            // DIAGNOSTIC (#219): log only the resolve/unresolve EDGES, so a flip
            // shows up as a single line instead of per-frame spam.
            if (resolved != scene_source_resolved_) {
                ctx_.logger.warn(
                    std::string("scene camera source ")
                    + (resolved ? "RESOLVED" : "UNRESOLVED (holding last view)")
                    + " entity=" + std::to_string(active_camera_entity_)
                    + " node_count=" + std::to_string(node_count)
                    + " behavior_scene="
                    + (behavior_scene_ ? "live" : "null"));
                scene_source_resolved_ = resolved;
            }

            if (resolved) {
                const wz::math::Mat4& world = wz::core::graph::node_data(
                    behavior_scene_->storage.polytree,
                    active_camera_entity_).world;

                // Extract the camera node's rigid pose from its world matrix
                // robustly. decompose_trs is intentionally NOT used here -- its
                // tight orthogonality/determinant gates reject the matrix on the
                // tiny FP drift accumulated through the tank's per-frame terrain-
                // alignment rotation, and a rejected decompose leaves an identity
                // pose, snapping the camera to the origin (the intermittent
                // "flip"). rigid_pose_from_matrix normalizes the basis (dropping
                // the parent's scale, e.g. the tank's 0.5) without that gate.
                const wz::math::Transform pose =
                    wz::math::rigid_pose_from_matrix(world);

                wz::bench::FlyingCamera cam{};
                cam.x = pose.position.x;
                cam.y = pose.position.y;
                cam.z = pose.position.z;
                cam.orientation = pose.rotation;

                const wz::math::Mat4 view = wz::bench::view_matrix(cam);
                const wz::math::Mat4 proj = wz::math::projection_perspective_dx(
                    active_camera_params_.fov_y,
                    aspect_,
                    active_camera_params_.near_plane,
                    active_camera_params_.far_plane);
                active_view_.view_projection = wz::math::mul(proj, view);
                active_view_.world_position =
                    wz::math::Vec3{ world.m[12], world.m[13], world.m[14] };
            }
            return;
        }

        // Free-fly source -> left-handed DX view-projection (the renderer's
        // convention). aspect tracks the window from the latest input.
        const wz::math::Mat4 view = wz::bench::view_matrix(camera_);
        const wz::math::Mat4 proj = wz::math::projection_perspective_dx(
            camera_fov_y_, aspect_, camera_near_, camera_far_);
        active_view_.view_projection = wz::math::mul(proj, view);
        active_view_.world_position =
            wz::math::Vec3{ camera_.x, camera_.y, camera_.z };
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
