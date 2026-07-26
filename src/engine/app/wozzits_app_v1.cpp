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
#include <engine/assets/data_table_csv_export.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/renderable/render_binding_sources.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/rhi_asset_identity.h>
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
#include <gpu/texture.h>
#include <gpu/dx12/dx12_internal.h>
#include <math/math_types.h>
#include <math/mat4.h>
#include <math/projection.h>
#include <scene/scene_graph.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wz::app
{
    namespace
    {

        // Decompose a simulation-node LOCAL matrix into an authored TRS. This is
        // the same lossy Mat4 -> TRS step the old per-frame write-back did; #221
        // moved it off the frame path so it runs only when document_.nodes() actually
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
        // Register the engine's built-in behavior modules up front. Project DLLs
        // are loaded later, in load_scene, from the manifest's
        // behavior_module_folder.
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
        // bind during load_scene, document_.nodes() is still empty, so this is a no-op
        // there; load_scene re-resolves after populating document_.nodes().
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
            wz::engine::assets::bridge_scene_renderable_keys(document_.nodes(), draft);
        // Re-assemble geometry+program bindings into renderables on every
        // (re)bind (#213 increment 2): mirrors bridge for the binding path, AFTER
        // it so the binding overrides a pre-built renderable on the same node. The
        // created renderables need their own commit()+resolve_all() (the main
        // resolve above already ran). No-op on the first bind during load_scene
        // (document_.nodes() empty); load_scene re-assembles after populating nodes.
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
            wz::engine::assets::bridge_scene_source_keys(document_.nodes(), draft);
        (void)scene_sources_bridged;
        // Re-point authored collision references at the freshly committed graph's
        // collision keys (issue #216/#217), mirroring the renderable/source
        // bridges above. No-op on the first bind during load_scene (document_.nodes()
        // empty); load_scene re-runs it after populating nodes.
        const uint32_t collisions_bridged =
            wz::engine::assets::bridge_scene_collision_keys(document_.nodes(), draft);
        (void)collisions_bridged;
        // Same for the authored atmosphere reference: a graph swap mints a new
        // Atmosphere key, so the frame's fog must follow the authored node id or
        // render_scene resolves nothing and the scene silently clears.
        const uint32_t atmospheres_bridged =
            wz::engine::assets::bridge_scene_atmosphere_keys(
                document_.nodes(), draft);
        (void)atmospheres_bridged;
        // Same for the authored FrameEnvironment reference (the successor to the
        // standalone atmosphere): follow the node id across a graph swap.
        const uint32_t environments_bridged =
            wz::engine::assets::bridge_scene_environment_keys(
                document_.nodes(), draft);
        (void)environments_bridged;
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
        // anyway so document_.nodes() is populated; the user can then fix the graph in
        // the editor and a later successful rebind will render. Bailing here left
        // document_.nodes() empty, so even a subsequent good compile drew nothing.

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

        // bind_asset_graph already ran above, but document_.nodes() was empty then;
        // now the scene is loaded, so bridge its renderables to the committed
        // graph keys. Populate document_.nodes() even with graph/scene compile errors
        // so a later good rebind can render.
        document_.nodes() = scene_data->nodes;
        // A new scene invalidates the prior scene's carried per-frame dispatch state
        // (#252 audit): clear the SELF_ACTIVATED edge-detector's previous-active
        // snapshot (else a reused authored id that was parked in the old scene and
        // active in the new one fires a spurious parked->live edge on the first
        // tick) and drop any pending spawn-with-identity requests the old scene left
        // unshipped.
        prev_active_by_id_.clear();
        spawn_identity_buffer_.clear();
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
            materialize_scene.nodes = document_.nodes();
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
            document_.nodes() = std::move(materialize_scene.nodes);
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
        document_.dirty() = false;
        document_.grafted_ids().clear();
        const uint32_t bridged =
            wz::engine::assets::bridge_scene_renderable_keys(
                document_.nodes(), graph_draft_);
        // Assemble renderables from geometry+program bindings now that
        // document_.nodes() is populated (#213 increment 1b): create the matching RHI
        // renderable per geometry node + set renderable_asset, the render program
        // inherited down the scene tree. The created assets compile in the shared
        // commit()+resolve_all() below (alongside the GLB scene sources).
        const std::size_t render_bindings_assembled =
            assemble_render_bindings(graph_draft_);
        // Resolve GLB scene-source DESCRIPTORS now that document_.nodes() is populated
        // (#213, the descriptor route): register each glb_scene_source's GLB +
        // produced Scene asset and write the Scene key into the node's
        // scene_source. The scene-from-json commit/resolve above already ran
        // (descriptors live on document_.nodes(), only available now), so compile the
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
                document_.nodes(), graph_draft_);
        // Re-point authored collision references at the bound graph's collision
        // keys (issue #216/#217), mirroring the renderable/source bridges; the
        // graph is committed by now (materialize ran above), so the referenced
        // collision node's key resolves.
        wz::engine::assets::bridge_scene_collision_keys(
            document_.nodes(), graph_draft_);
        // Same for the authored atmosphere reference, so the frame's fog is
        // resolved before the first render_scene of a freshly loaded scene.
        wz::engine::assets::bridge_scene_atmosphere_keys(
            document_.nodes(), graph_draft_);
        // Same for the authored FrameEnvironment reference, so the frame's
        // environment is resolved before the first render_scene of a load.
        wz::engine::assets::bridge_scene_environment_keys(
            document_.nodes(), graph_draft_);
        // Flatten any glb_scene_source node authored with consume_mode=Flatten:
        // expand persistently (and drop the descriptor), exactly like the editor
        // "bake" action, so a Flatten-authored scene loads as real nodes. The
        // remaining Instance descriptors are grafted as live children below.
        for (const wz::engine::assets::SceneNodeAsset& node :
             std::vector<wz::engine::assets::SceneNodeAsset>(document_.nodes()))
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
        // modules, then materialize the scene's behavior runtime. The modules come
        // from the project manifest's behavior_module_folder (the built-ins were
        // already registered in the ctor).
        load_behavior_modules(desc.behavior_module_folder);
        // Bake draw order into the flat node array: the renderer walks it linearly
        // (no per-frame sort), so ordering must live in the array itself. Draw
        // order defaults to the tree's pre-order and render_order overrides it as
        // a coarse layer. Done once here, after all authored nodes are present and
        // before the polytree is built from them; structural edits re-bake below.
        wz::engine::assets::bake_scene_node_draw_order(document_.nodes());
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

        // Editor viewport only (standalone play uses the authored scene camera):
        // seed the free-fly camera from the scene file's editor metadata so the
        // project reopens looking from where it was left. The metadata is read
        // straight from the raw scene file -- it is intentionally NOT part of the
        // SceneAssetData game-scene model -- and an absent/partial block leaves the
        // camera at its current pose (its default on first load).
        if (!view_.prefer_scene_camera()) {
            const wz::fs::Path resource_root =
                ctx_.assets ? ctx_.assets->resource_root() : wz::fs::Path{};
            const wz::fs::Path scene_file =
                wz::fs::is_absolute(desc.scene) || resource_root.empty()
                    ? desc.scene
                    : wz::fs::join(resource_root, desc.scene);
            std::ifstream file(scene_file, std::ios::binary);
            if (file) {
                const std::string text(
                    (std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
                const wz::json::JSONParseResult parsed =
                    wz::json::parse_json_string(text);
                if (parsed.ok && parsed.document.root) {
                    if (const std::optional<
                            wz::engine::assets::SceneEditorCameraMetadata>
                            meta =
                                wz::engine::assets::
                                    read_scene_document_editor_camera(
                                        parsed.document))
                    {
                        wz::bench::FlyingCamera& camera = view_.free_fly_camera();
                        camera.x = meta->position[0];
                        camera.y = meta->position[1];
                        camera.z = meta->position[2];
                        camera.orientation = { meta->orientation[0],
                            meta->orientation[1], meta->orientation[2],
                            meta->orientation[3] };
                        camera.move_speed       = meta->move_speed;
                        camera.look_speed       = meta->look_speed;
                        camera.boost_multiplier = meta->boost_multiplier;
                        camera.roll_speed       = meta->roll_speed;
                        ctx_.logger.info(
                            "load_scene: restored editor viewport camera");
                    }
                }
            }
        }
        view_.clear_editor_camera_dirty();

        return graph_ok && scene_resolve.ok();
    }

    void WozzitsApp_v1::simulation_tick(
        const wz::input::InputState& input, float dt, bool drive_camera)
    {
        rematerialize_count_this_frame_ = 0;
        rebuild_scene_count_this_frame_ = 0;
        remat_callers_this_frame_.clear();
        const std::chrono::steady_clock::time_point sim_started =
            std::chrono::steady_clock::now();

        // The fly-cam consumes input only when the host arms it (drive_camera);
        // behaviors below always get the input, so a controller can drive the
        // scene without panning the camera. aspect tracking is independent.
        // update_free_fly moves the camera and marks the editor-camera-dirty flag
        // on a real pose change (editor mode only).
        if (drive_camera) {
            view_.update_free_fly(input, dt);
        }
        if (input.window.width > 0 && input.window.height > 0) {
            view_.set_aspect(static_cast<float>(input.window.width)
                / static_cast<float>(input.window.height));
        }

        // Run the scene's behaviors before render prep so this frame draws the
        // post-behavior transforms (dispatch + command-apply happen ahead of render
        // binding assembly). No-op without a live behavior scene.
        dispatch_scene_behaviors(input, dt);

        // Advances the renderer's animation clock by the real frame delta. The
        // render path only READS it, so the showcase's extra offscreen
        // render_scene no longer doubles the rate of every animated thing
        // (#282).
        renderer_.simulation_tick(dt);

        // Both camera sources are now current (free-fly updated from input above;
        // behaviors moved the scene-camera node in dispatch_scene_behaviors).
        // Materialize the single active view render_scene reads -- no work happens
        // in the render path.
        materialize_active_view();

        // Per-tick audio spatialization (play mode only, and only when the audio
        // device is actually running). Retunes already-playing Clip AudioSources
        // from the active listener's pose: pan + ITD + distance + Doppler. It
        // never starts a voice, so it's a harmless no-op for finished one-shots.
        // Needs the behavior scene (the runtime audio_sources/audio_listeners +
        // the runtime→authored map) and the nodes' world transforms.
        if (view_.prefer_scene_camera() && audio_runtime_.running()
            && behavior_scene_ && ctx_.assets) {
            // #221: the pass reads source/listener world poses straight from the
            // behavior scene's polytree (the same single source of truth
            // scene_world_transforms() draws from), so it needs neither the
            // document_.nodes() span nor a precomputed world-transform vector here.
            wz::engine::audio::update_scene_audio_spatialization(
                *ctx_.assets, *behavior_scene_,
                dt, audio_runtime_.output_sample_rate(),
                audio_runtime_.scheduler(), audio_spatialization_);
        }

        // Per-frame rebuild profiling (#252): warn on redundant structural work and
        // record a sample for the CSV (behavior_frame_index_ advanced above).
        const double sim_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - sim_started).count();
        if (rematerialize_count_this_frame_ > 1
            || rebuild_scene_count_this_frame_ > 1)
        {
            ctx_.logger.warn(
                "[perf] frame " + std::to_string(behavior_frame_index_)
                + ": redundant structural rebuilds -- rematerialize x"
                + std::to_string(rematerialize_count_this_frame_)
                + " rebuild_behavior_scene x"
                + std::to_string(rebuild_scene_count_this_frame_)
                + " (" + std::to_string(sim_ms) + " ms)");
        }
        if (frame_profiling_enabled_ && frame_profile_.size() < 200000u) {
            frame_profile_.push_back(FrameProfileSample{
                .frame = behavior_frame_index_,
                .dt_ms = static_cast<double>(dt) * 1000.0,
                .sim_ms = sim_ms,
                .scene_nodes = document_.nodes().size(),
                .rematerialize = rematerialize_count_this_frame_,
                .rebuild = rebuild_scene_count_this_frame_,
                .callers = remat_callers_this_frame_,
            });
        }
    }

    std::vector<SceneletCatalogEntry> WozzitsApp_v1::scenelet_catalog() const
    {
        return scenelet_catalog_;
    }

    std::size_t WozzitsApp_v1::resolve_glb_scene_sources()
    {
        if (!ctx_.assets) {
            return 0;
        }

        std::size_t resolved = 0;
        for (wz::engine::assets::SceneNodeAsset& node : document_.nodes()) {
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
        const wz::asset::AssetGraphDraft& draft,
        const std::string* only_node,
        const std::unordered_set<std::string>* only_nodes)
    {
        if (!ctx_.assets) {
            return 0;
        }

        // Resolve a graph node id to its committed output key + asset type,
        // mirroring bridge_scene_renderable_keys. A DELETED draft node is
        // unresolved (#229 bridge-reset semantics): its entry lingers in
        // node_index, but handing out its stale key would wire the assembled
        // renderable to an asset the bind no longer registers.
        const auto resolve_graph_node =
            [&draft](
                wz::asset::AssetGraphDraftNodeId id,
                wz::asset::AssetKey& out_key,
                wz::asset::AssetType& out_type) -> bool {
                const auto it = draft.node_index.find(id);
                if (it == draft.node_index.end()) {
                    return false;
                }
                const wz::asset::AssetGraphDraftNode& node =
                    draft.nodes[it->second];
                if (node.state == wz::asset::AssetGraphDraftNodeState::Deleted)
                {
                    return false;
                }
                out_key = node.node.key;
                out_type = node.node.type;
                return true;
            };

        // Nearest scene node with the given id (linear scan; scenes are small).
        const auto find_node =
            [this](const std::string& id)
                -> const wz::engine::assets::SceneNodeAsset* {
                for (const auto& n : document_.nodes()) {
                    if (n.id == id) {
                        return &n;
                    }
                }
                return nullptr;
            };

        std::size_t assembled = 0;
        for (wz::engine::assets::SceneNodeAsset& node : document_.nodes()) {
            // Incremental (#253/#252): when a single node (only_node) or a subtree
            // (only_nodes, the spawned block + its grafted children) is targeted,
            // skip the rest -- the bare loop + id compare is trivial; only the
            // targeted nodes do the expensive resolve/create-renderable work.
            // Ancestor-program lookups below still scan the full span, so program
            // inheritance stays correct even when the ancestor is off-target.
            if (only_node && node.id != *only_node) {
                continue;
            }
            if (only_nodes && only_nodes->count(node.id) == 0) {
                continue;
            }
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

            // Bridge the node's custom-renderable semantic bindings (issue
            // #229), mirroring the audio bridge: clear the stale resolved key
            // first (a deleted source must stop feeding the synthesized
            // renderable), resolve the stable graph anchor, and sanity-check
            // the target — a type that PUBLISHES no GPU resource for the
            // named semantic (render_binding_sources.h) is warned + skipped.
            // Full kind-vs-layout validation happens at the 0x70A compile
            // (which needs the program's layout); an unparseable semantic
            // string passes through so that compile can name it.
            for (wz::engine::assets::SceneRenderableSemanticBinding& binding :
                 node.renderable_bindings)
            {
                binding.asset = {};  // clear stale
                if (!binding.asset_graph_node_id) {
                    continue;
                }
                wz::asset::AssetKey k{};
                wz::asset::AssetType t{};
                if (!resolve_graph_node(*binding.asset_graph_node_id, k, t)) {
                    ctx_.logger.warn(
                        "assemble_render_bindings: node '" + node.id
                        + "' binding '" + binding.semantic
                        + "' asset-graph node not found (skipped)");
                    continue;
                }
                const std::optional<wz::engine::assets::DescriptorSemantic>
                    semantic =
                        wz::engine::assets::descriptor_semantic_from_name(
                            binding.semantic);
                if (semantic
                    && !wz::engine::assets::render_binding_source_for(
                        t, *semantic))
                {
                    ctx_.logger.warn(
                        "assemble_render_bindings: node '" + node.id
                        + "' binding '" + binding.semantic
                        + "' target publishes no GPU resource for that "
                          "semantic (skipped)");
                    continue;
                }
                binding.asset = k;
            }

            // Bridge a render-to-texture source's target anchor (issue #287),
            // same shape as the audio-renderable bridge: clear the stale key so
            // a deleted target stops being drawn into, resolve the stable graph
            // anchor, and require a Texture -- anything else has no render
            // target behind it, and rendering into it would fail per frame
            // rather than once, here, with the node's name.
            if (node.render_to_texture) {
                node.render_to_texture->target = {};  // clear stale
                if (node.render_to_texture->target_node_id) {
                    wz::asset::AssetKey k{};
                    wz::asset::AssetType t{};
                    if (!resolve_graph_node(
                            *node.render_to_texture->target_node_id, k, t)) {
                        ctx_.logger.warn(
                            "assemble_render_bindings: node '" + node.id
                            + "' render_to_texture target asset-graph node not "
                              "found (renders nowhere)");
                    }
                    else if (t != wz::engine::assets::kAssetTypeTexture) {
                        ctx_.logger.warn(
                            "assemble_render_bindings: node '" + node.id
                            + "' render_to_texture target is not a texture "
                              "(renders nowhere)");
                    }
                    else {
                        node.render_to_texture->target = k;
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
                 cur && guard <= document_.nodes().size();
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
            const bool wants_custom = !node.renderable_bindings.empty()
                || !node.renderable_constants.empty();
            wz::engine::assets::RenderableAsset renderable{};
            if (geometry_type == wz::engine::assets::kAssetTypeMesh
                && wants_custom)
            {
                // Custom-renderable ingredients present (issue #229):
                // synthesize the CUSTOM (0x70A) recipe instead of the bare
                // pull mesh. Rows pass through even half-resolved (semantic
                // with no key after the bridge above) so the compile can NAME
                // the failure. Instance constant OVERRIDES deliberately stay
                // off the desc — they merge at pack time, so editing one
                // never re-keys the synthesized asset.
                wz::engine::assets::CustomRenderableDesc desc{};
                desc.name = "render_binding/" + node.id;
                desc.mesh = wz::engine::assets::MeshAsset{
                    .output = geometry_key };
                desc.program = wz::engine::assets::RenderProgramAsset{
                    .key = program_key };
                desc.bindings.reserve(node.renderable_bindings.size());
                for (const auto& binding : node.renderable_bindings) {
                    desc.bindings.push_back(
                        wz::engine::assets::CustomRenderableCompileDesc::
                            Binding{
                                .semantic = binding.semantic,
                                .asset = binding.asset,
                            });
                }
                renderable =
                    ctx_.assets->renderables().create_custom_renderable(desc);
            }
            else if (geometry_type == wz::engine::assets::kAssetTypeMesh) {
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
                if (wants_custom) {
                    ctx_.logger.warn(
                        "assemble_render_bindings: node '" + node.id
                        + "' has renderable bindings/constants but its "
                          "geometry is a gpu_sparse_mesh; the custom (0x70A) "
                          "recipe requires Mesh geometry, so they are "
                          "ignored");
                }
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

    std::optional<wz::engine::assets::AuthoredTransform>
    WozzitsApp_v1::derived_authored_transform(
        const wz::scene::AuthoredEntityId& id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), id);
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
            wz::engine::assets::find_scene_node(document_.nodes(), id);
        if (!node) {
            return std::nullopt;
        }
        // The raw stored authored transform — deliberately NOT derived from the
        // sim polytree (#221), so a test can prove document_.nodes() stays put while
        // the sim moves the node.
        return wz::math::Vec3{
            node->local.translation[0],
            node->local.translation[1],
            node->local.translation[2],
        };
    }

    std::optional<wz::asset::AssetKey> WozzitsApp_v1::node_renderable_asset(
        const wz::scene::AuthoredEntityId& id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), id);
        if (!node || !node->renderable_asset) {
            return std::nullopt;
        }
        return *node->renderable_asset;
    }

    void WozzitsApp_v1::apply_node_local_transform(
        const wz::scene::AuthoredEntityId& id,
        const wz::engine::assets::AuthoredTransform& transform)
    {
        // #221: the ONE place a transform edit lands. The polytree is the single
        // source of truth for the drawn + saved pose (scene_world_transforms /
        // derived_authored_transform read it, and rebuild_behavior_scene restores
        // a surviving node's live local by authored id), so write the LOCAL there
        // and do NOT touch document_.nodes()'s stored transform. For a sim-driven node
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
                document_.dirty() = true;
                return;
            }
        }

        // DEGENERATE fallback: no live polytree entry for this node (a failed
        // instantiate_scene left behavior_scene_ null, or the node is absent from
        // the runtime map). scene_world_transforms / derived_authored_transform
        // fall back to document_.nodes() in exactly that case, so write the stored
        // transform here so the edit still takes effect and the editor recovers.
        if (wz::engine::assets::SceneNodeAsset* node =
                wz::engine::assets::find_scene_node(document_.nodes(), id)) {
            wz::engine::assets::set_transform(*node, transform);
            document_.dirty() = true;
        }
    }

    bool WozzitsApp_v1::set_node_transform(
        const wz::scene::AuthoredEntityId& id,
        const wz::engine::assets::AuthoredTransform& transform)
    {
        // Resolve existence, then route through the single #221 edit seam. The
        // document_.nodes() transform write is gone from the edit path: Phase 1's
        // derivation covers persistence (save_scene + authored_scene_nodes derive
        // from the polytree) and rebuilds restore live locals via the
        // preservation map, so the polytree write the seam does is sufficient.
        if (!wz::engine::assets::find_scene_node(document_.nodes(), id)) {
            return false;
        }
        apply_node_local_transform(id, transform);
        return true;
    }

    void WozzitsApp_v1::apply_scene_change(const SceneChange& change)
    {
        // Map a document edit's SceneChange to the reaction that keeps the runtime
        // + renderer consistent (#258 avenue 2). Kinds are handled as the edit
        // verbs are converted to emit descriptors; a verb still reacting inline
        // does not route through here yet.
        switch (change.kind) {
        case SceneChangeKind::None:
            // Pure document edit: the renderer + behavior runtime read the authored
            // fields fresh next frame, so there is nothing to do now.
            break;
        case SceneChangeKind::Structural:
            // A structural edit invalidates the behavior runtime's entity ids;
            // re-materialize it when one is live (an edit to a scene with no live
            // runtime just settles into the authored document).
            if (behavior_scene_) {
                rebuild_behavior_scene();
            }
            break;
        case SceneChangeKind::RuntimeRebuild:
            // A behavior binding or added sim component re-materializes
            // UNCONDITIONALLY: adding the first to a scene that had no runtime must
            // create it.
            rebuild_behavior_scene();
            break;
        case SceneChangeKind::Collision:
            // Re-bridge the collision refs to the bound graph's keys, then
            // re-materialize so the runtime's collision world has the surface.
            wz::engine::assets::bridge_scene_collision_keys(
                document_.nodes(), graph_draft_);
            rebuild_behavior_scene();
            break;
        case SceneChangeKind::SceneSource:
            // Re-bridge the node-ref key, re-graft the host's children (mutates the
            // document), assemble the grafted subtree, then rebuild the runtime.
            wz::engine::assets::bridge_scene_source_keys(
                document_.nodes(), graph_draft_);
            graft_scene_sources();
            rematerialize_render_bindings(change.caller);
            rebuild_behavior_scene();
            break;
        case SceneChangeKind::GlbSource:
            // Re-resolve the GLB descriptor -> Scene, re-graft + assemble + rebuild.
            rematerialize_glb_scene_sources();
            break;
        case SceneChangeKind::RenderBinding:
            // The renderable recipe changed; re-assemble the bindings. The verb's
            // call site rides in the descriptor so the #252 profile still names it.
            rematerialize_render_bindings(change.caller);
            break;
        case SceneChangeKind::RenderBindingNode:
            // Only one node's recipe changed; re-assemble just it (#253).
            rematerialize_node_render_binding(change.node_id);
            break;
        }
    }

    // The structural edit verbs delegate the document mutation to document_ (which
    // returns a SceneEdit: the mutation result + the SceneChange), then dispatch
    // the reaction. Mutation lives in SceneDocument, reaction in the host.
    wz::engine::assets::SceneAddChildResult WozzitsApp_v1::add_child_node(
        const wz::scene::AuthoredEntityId& parent_id)
    {
        SceneEdit<wz::engine::assets::SceneAddChildResult> edit =
            document_.add_child(parent_id);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_properties(
        const wz::scene::AuthoredEntityId& id,
        std::string name,
        bool visible)
    {
        const SceneEdit<bool> edit =
            document_.set_properties(id, std::move(name), visible);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::reparent_node(
        const wz::scene::AuthoredEntityId& id,
        const wz::scene::AuthoredEntityId& new_parent_id)
    {
        const SceneEdit<bool> edit = document_.reparent(id, new_parent_id);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::remove_node(const wz::scene::AuthoredEntityId& id)
    {
        const SceneEdit<bool> edit = document_.remove(id);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::reorder_node(
        const wz::scene::AuthoredEntityId& id,
        const wz::scene::AuthoredEntityId& before_id)
    {
        const SceneEdit<bool> edit = document_.reorder(id, before_id);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_render_order(
        const wz::scene::AuthoredEntityId& id,
        int render_order)
    {
        const SceneEdit<bool> edit = document_.set_render_order(id, render_order);
        apply_scene_change(edit.change);
        return edit.result;
    }

    // ─── Live behavior-binding authoring ────────────────────────────────────
    // Each applies the matching scene_asset_data.h helper to document_.nodes(), then
    // (on success) marks the scene dirty and re-materializes the behavior
    // runtime so the change takes effect. The rebuild is UNCONDITIONAL on
    // success — unlike the structural edits above, which rebuild only when a
    // behavior scene already exists — because adding the first binding to a node
    // that had none must create the behavior runtime where there was none.

    // The behavior-binding verbs delegate the mutation to document_ and dispatch
    // the RuntimeRebuild it asks for on success.
    wz::engine::assets::SceneAddBehaviorResult WozzitsApp_v1::add_node_behavior(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& module)
    {
        SceneEdit<wz::engine::assets::SceneAddBehaviorResult> edit =
            document_.add_behavior(node_id, module);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::remove_node_behavior(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id)
    {
        const SceneEdit<bool> edit =
            document_.remove_behavior(node_id, binding_id);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_behavior_enabled(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        bool enabled)
    {
        const SceneEdit<bool> edit =
            document_.set_behavior_enabled(node_id, binding_id, enabled);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_behavior_fields(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const std::string& label,
        const std::string& module)
    {
        const SceneEdit<bool> edit =
            document_.set_behavior_fields(node_id, binding_id, label, module);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_behavior_events(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const std::vector<std::string>& events)
    {
        const SceneEdit<bool> edit =
            document_.set_behavior_events(node_id, binding_id, events);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_behavior_config(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const wz::engine::assets::SceneBehaviorConfigValue& value)
    {
        const SceneEdit<bool> edit =
            document_.set_behavior_config(node_id, binding_id, value);
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::clear_node_behavior_config(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const std::string& key)
    {
        const SceneEdit<bool> edit =
            document_.clear_behavior_config(node_id, binding_id, key);
        apply_scene_change(edit.change);
        return edit.result;
    }

    // ─── Live optional-component authoring ──────────────────────────────────
    // Add/remove one of the five editor-managed optional components on a node in
    // document_.nodes(), then (on success) mark the scene dirty. Unlike the behavior
    // verbs above, these do NOT rebuild_behavior_scene(): none of camera /
    // renderable / proximity / collision / motion creates a behavior binding, so
    // the behavior runtime is unaffected. The renderer reads document_.nodes() fresh
    // each frame, so the next render reflects the change. An unknown kind (or
    // missing node) is a logged no-op (fail closed).

    // The optional-component verbs delegate the mutation to document_ (a None
    // change: no runtime reaction) and keep their own no-op warning log.
    bool WozzitsApp_v1::add_node_component(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& kind)
    {
        const SceneEdit<bool> edit = document_.add_component(node_id, kind);
        if (!edit.result) {
            ctx_.logger.warn(
                "add_node_component: no-op (node '" + node_id
                + "' missing or unknown component kind '" + kind + "')");
        }
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::remove_node_component(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& kind)
    {
        const SceneEdit<bool> edit = document_.remove_component(node_id, kind);
        if (!edit.result) {
            ctx_.logger.warn(
                "remove_node_component: no-op (node '" + node_id
                + "' missing or unknown component kind '" + kind + "')");
        }
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_renderable_asset(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        const SceneEdit<bool> edit =
            document_.set_renderable_asset(node_id, asset_graph_node_id);
        if (!edit.result) {
            ctx_.logger.warn(
                "set_node_renderable_asset: no-op (node '" + node_id
                + "' missing)");
        }
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_audio_renderable(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        // The node id resolves to the audio_renderable key in
        // assemble_render_bindings on the next bind; nothing to draw, so no
        // immediate rematerialize is needed (mirrors set_node_renderable_asset).
        const SceneEdit<bool> edit =
            document_.set_audio_renderable(node_id, asset_graph_node_id);
        if (!edit.result) {
            ctx_.logger.warn(
                "set_node_audio_renderable: no-op (node '" + node_id
                + "' missing)");
        }
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_audio_source_play(
        const wz::scene::AuthoredEntityId& node_id,
        bool auto_play,
        bool enabled)
    {
        const SceneEdit<bool> edit =
            document_.set_audio_source_play(node_id, auto_play, enabled);
        if (!edit.result) {
            ctx_.logger.warn(
                "set_node_audio_source_play: no-op (node '" + node_id
                + "' missing or has no AudioSource)");
        }
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_scene_source(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        // The SceneSource reaction re-resolves the source against the bound graph,
        // re-grafts the host's children, assembles the grafted subtree, and
        // rebuilds the runtime (a cleared id drops the stale children).
        const SceneEdit<bool> edit =
            document_.set_scene_source(node_id, asset_graph_node_id);
        if (!edit.result) {
            ctx_.logger.warn(
                "set_node_scene_source: no-op (node '" + node_id + "' missing)");
        }
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_geometry_asset(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        const SceneEdit<bool> edit =
            document_.set_geometry_asset(node_id, asset_graph_node_id);
        if (!edit.result) {
            ctx_.logger.warn(
                "set_node_geometry_asset: no-op (node '" + node_id
                + "' missing)");
        }
        apply_scene_change(edit.change);
        return edit.result;
    }

    bool WozzitsApp_v1::set_node_render_program(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
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
        document_.dirty() = true;
        // If this targets a runtime-only grafted scene-source child, mirror the
        // program onto its host as a sticky override (issue #213) so it survives
        // reload (save_scene excludes grafted children). No-op for authored nodes.
        // (A cross-node document write -- part of the mutation, not the reaction.)
        capture_grafted_child_override(node_id);
        // A program change cascades to descendants via inheritance, so
        // re-assemble every binding (assemble walks ancestors per node).
        apply_scene_change(SceneChange::render_binding());
        return true;
    }

    bool WozzitsApp_v1::set_node_renderable_binding(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& semantic,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_renderable_binding: no-op (node '" + node_id
                + "' missing)");
            return false;
        }
        if (semantic.empty()) {
            ctx_.logger.warn(
                "set_node_renderable_binding: no-op (empty semantic on node '"
                + node_id + "')");
            return false;
        }

        auto& bindings = node->renderable_bindings;
        const auto it = std::find_if(
            bindings.begin(), bindings.end(),
            [&semantic](
                const wz::engine::assets::SceneRenderableSemanticBinding& b) {
                return b.semantic == semantic;
            });
        if (asset_graph_node_id != 0) {
            if (it != bindings.end()) {
                it->asset_graph_node_id = asset_graph_node_id;
                it->asset = {};  // stale until the re-assembly bridges it
            }
            else {
                bindings.push_back(
                    wz::engine::assets::SceneRenderableSemanticBinding{
                        .semantic = semantic,
                        .asset_graph_node_id = asset_graph_node_id,
                    });
            }
        }
        else if (it != bindings.end()) {
            bindings.erase(it);
        }

        document_.dirty() = true;
        // A binding decides whether the assembled renderable is the custom
        // (0x70A) form, so re-assemble like the geometry/program seams.
        apply_scene_change(SceneChange::render_binding());
        return true;
    }

    bool WozzitsApp_v1::set_node_renderable_constant(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& name,
        const float* value)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_renderable_constant: no-op (node '" + node_id
                + "' missing)");
            return false;
        }
        if (name.empty()) {
            ctx_.logger.warn(
                "set_node_renderable_constant: no-op (empty name on node '"
                + node_id + "')");
            return false;
        }

        auto& constants = node->renderable_constants;
        const auto it = std::find_if(
            constants.begin(), constants.end(),
            [&name](
                const wz::engine::assets::SceneRenderableConstantOverride& c) {
                return c.name == name;
            });
        const bool existed = it != constants.end();
        if (value) {
            wz::engine::assets::SceneRenderableConstantOverride* target =
                existed
                    ? &*it
                    : &constants.emplace_back(
                          wz::engine::assets::SceneRenderableConstantOverride{
                              .name = name });
            std::copy(value, value + 4, target->value);
        }
        else if (existed) {
            constants.erase(it);
        }

        // Instance overrides merge at PACK time from the node — no
        // re-assembly, no recompile, no re-key. The one exception: the FIRST
        // override on a GEOMETRY node with no bindings flips its SYNTHESIZED
        // renderable from the plain pull-mesh recipe to the custom (0x70A) form
        // (and the last removal flips it back), which IS an assembly change. A
        // node drawn by a PRE-BUILT renderable (renderable_asset_node_id, e.g.
        // the cannon FX) has no synthesized recipe to flip — assemble_render_-
        // bindings skips it (no geometry) — so its constant just merges at pack
        // time and a rematerialize there is pure O(scene) waste (#252/#253).
        const bool custom_form_flipped =
            node->geometry_asset_node_id
            && node->renderable_bindings.empty()
            && ((value && !existed && constants.size() == 1u)
                || (!value && existed && constants.empty()));
        document_.dirty() = true;
        // The kind is decided by POST-mutation document state: only the custom-
        // form flip needs a re-assemble (of just this node, #253); a plain
        // override merges at pack time with no reaction.
        apply_scene_change(
            custom_form_flipped
                ? SceneChange::render_binding_node(node->id)
                : SceneChange::none());
        return true;
    }

    std::optional<std::array<float, 4>>
    WozzitsApp_v1::node_renderable_constant(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& name) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        if (!node) {
            return std::nullopt;
        }
        for (const wz::engine::assets::SceneRenderableConstantOverride& c :
             node->renderable_constants)
        {
            if (c.name == name) {
                return std::array<float, 4>{
                    c.value[0], c.value[1], c.value[2], c.value[3] };
            }
        }
        return std::nullopt;
    }

    bool WozzitsApp_v1::set_node_collision_asset(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id,
        bool constrain_movement)
    {
        // The Collision reaction re-bridges the collision key + rebuilds the
        // SceneInstance whose collision world the constraint loop reads.
        const SceneEdit<bool> edit = document_.set_collision_asset(
            node_id, asset_graph_node_id, constrain_movement);
        if (!edit.result) {
            ctx_.logger.warn(
                "set_node_collision_asset: no-op (node '" + node_id
                + "' missing)");
        }
        apply_scene_change(edit.change);
        return edit.result;
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
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
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
        document_.dirty() = true;

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
        bool patched = false;
        if (!adding_component && behavior_scene_) {
            const auto it = behavior_scene_->authored_to_runtime.find(node_id);
            if (it != behavior_scene_->authored_to_runtime.end()) {
                for (auto& record : behavior_scene_->motions) {
                    if (record.node != it->second) {
                        continue;
                    }
                    // Only the authored terrain-stick fields; terrain_alignment_-
                    // rate (runtime-only) and velocities are untouched. This dual
                    // write (authored field + live record) stays inline: it mutates
                    // the runtime, which the document has no handle on.
                    record.component.terrain_constrained = terrain_constrained;
                    record.component.terrain_ride_height = ride_height;
                    record.component.terrain_footprint_radius = footprint_radius;
                    record.component.terrain_align_to_surface = align_to_surface;
                    record.component.terrain_alignment_strength =
                        alignment_strength;
                    patched = true;
                    break;
                }
            }
        }

        // Patched the live record in place -> no reaction. Otherwise adding the
        // component (or, defensively, no matching live record) needs a rebuild so
        // the Motion record participates in integrate_motion + constraints.
        apply_scene_change(
            patched ? SceneChange::none() : SceneChange::runtime_rebuild());
        return true;
    }

    bool WozzitsApp_v1::set_node_motion_filter(
        const wz::scene::AuthoredEntityId& node_id,
        const wz::engine::assets::SceneMotionFilterAsset& filter)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_motion_filter: no-op (node '" + node_id
                + "' missing)");
            return false;
        }
        const bool adding_component = !node->motion_filter.has_value();
        node->motion_filter = filter;
        document_.dirty() = true;

        // Patch the LIVE motion_filter record in place when it already exists (a
        // field tweak must not rebuild + snap sim actors). The filter STATE lives
        // in motion_filter_states_ (keyed by stable id), so it is untouched either
        // way. Adding the component needs a rebuild so the record is materialized
        // into behavior_scene_->motion_filters.
        bool patched = false;
        if (!adding_component && behavior_scene_) {
            const auto it = behavior_scene_->authored_to_runtime.find(node_id);
            if (it != behavior_scene_->authored_to_runtime.end()) {
                for (auto& record : behavior_scene_->motion_filters) {
                    if (record.node == it->second) {
                        record.component = filter;
                        patched = true;
                        break;
                    }
                }
            }
        }

        apply_scene_change(
            patched ? SceneChange::none() : SceneChange::runtime_rebuild());
        return true;
    }

    bool WozzitsApp_v1::set_node_camera(
        const wz::scene::AuthoredEntityId& node_id,
        const wz::engine::assets::SceneCameraAsset& camera)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_camera: no-op (node '" + node_id + "' missing)");
            return false;
        }
        node->camera = camera;
        document_.dirty() = true;

        // Rebuild so the live view controller re-reads the camera params next
        // frame. Camera edits are edit-time and coalesced by id, so a rebuild per
        // service cycle is fine; a lighter in-place patch (as the motion filter
        // does for a field tweak) could replace this later if it ever matters.
        apply_scene_change(SceneChange::runtime_rebuild());
        return true;
    }

    bool WozzitsApp_v1::set_node_atmosphere(
        const wz::scene::AuthoredEntityId& node_id,
        const wz::engine::assets::SceneAtmosphereAsset& atmosphere)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_atmosphere: no-op (node '" + node_id + "' missing)");
            return false;
        }
        node->atmosphere = atmosphere;
        document_.dirty() = true;

        // Rebuild so the renderer re-resolves the frame atmosphere next frame (the
        // atmosphere_asset key is re-bridged from atmosphere_asset_node_id on the
        // rebind). Edits are coalesced by id, so a rebuild per service cycle is fine.
        apply_scene_change(SceneChange::runtime_rebuild());
        return true;
    }

    bool WozzitsApp_v1::set_node_environment(
        const wz::scene::AuthoredEntityId& node_id,
        const wz::engine::assets::SceneEnvironmentAsset& environment)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        if (!node) {
            ctx_.logger.warn(
                "set_node_environment: no-op (node '" + node_id + "' missing)");
            return false;
        }
        node->environment = environment;
        document_.dirty() = true;

        // Rebuild so the renderer re-resolves the frame environment next frame (the
        // environment_asset key is re-bridged from environment_asset_node_id on the
        // rebind). Edits are coalesced by id, so a rebuild per service cycle is fine.
        apply_scene_change(SceneChange::runtime_rebuild());
        return true;
    }

    // Extract a short seam identifier from a source_location function name --
    // "void wz::app::WozzitsApp_v1::set_node_renderable_program(...)" -> the bare
    // "set_node_renderable_program". CSV-safe (a bare identifier: no comma) and
    // stable across line shifts, so it can name WHICH call site forced a
    // rematerialize in the frame_profile "remat_callers" column (#252).
    static std::string short_render_caller(const char* fn)
    {
        std::string s = fn ? fn : "?";
        if (const std::size_t paren = s.find('('); paren != std::string::npos) {
            s.resize(paren);
        }
        if (const std::size_t colons = s.rfind("::");
            colons != std::string::npos) {
            s.erase(0, colons + 2);
        }
        if (const std::size_t space = s.rfind(' '); space != std::string::npos) {
            s.erase(0, space + 1);  // drop any leftover return-type prefix
        }
        return s;
    }

    void WozzitsApp_v1::rematerialize_render_bindings(std::source_location caller)
    {
        ++rematerialize_count_this_frame_;
        // Accumulate a short caller label per frame for the frame_profile
        // "remat_callers" CSV column -- the CSV is the artifact the user hands
        // back (the editor never writes the play log to a file), so the
        // WHICH-seam answer to the spurious burst must live in the CSV (#252).
        if (!remat_callers_this_frame_.empty()) {
            remat_callers_this_frame_ += ";";
        }
        remat_callers_this_frame_ += short_render_caller(caller.function_name());
        // Name the caller so a play log can attribute WHICH edit forced the
        // re-materialize -- pins the spurious spawn-time burst (4x in one frame
        // with nothing structural changed; #252). Rare (a few frames/session),
        // so an unconditional line is not noisy.
        ctx_.logger.info(
            std::string("[perf] rematerialize_render_bindings frame ")
            + std::to_string(behavior_frame_index_) + " caller="
            + caller.function_name() + " ("
            + std::to_string(caller.line()) + ")");
        if (!ctx_.assets) {
            return;
        }
        // Re-bridge the pre-built renderables first (so a node that lost its
        // geometry can fall back to renderable_asset_node_id if it has one), then
        // re-assemble the ingredient bindings (which override for geometry nodes).
        wz::engine::assets::bridge_scene_renderable_keys(
            document_.nodes(), graph_draft_);
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

    void WozzitsApp_v1::rematerialize_node_render_binding(
        const std::string& node_id)
    {
        if (!ctx_.assets) {
            return;
        }
        // Incremental (#253): assemble only this node's renderable (its recipe
        // changed) -- no full-scene re-bridge/re-assemble. commit + resolve_all
        // materialize the (re)synthesized asset; resolve is cache-hit for every
        // other node, so only the changed node does real work.
        const std::size_t assembled =
            assemble_render_bindings(graph_draft_, &node_id);
        if (assembled > 0) {
            ctx_.assets->commit();
            const wz::engine::assets::ResolveReport resolve =
                ctx_.assets->resolve_all();
            if (!resolve.ok()) {
                ctx_.logger.warn(
                    "rematerialize_node_render_binding: resolved with errors="
                    + std::to_string(resolve.failures.size()));
            }
        }
    }

    bool WozzitsApp_v1::set_node_glb_scene_source(
        const wz::scene::AuthoredEntityId& node_id,
        const wz::engine::assets::SceneGLBSceneSource& descriptor)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
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
        document_.dirty() = true;

        // Re-materialize so the change shows on the next frame. The GlbSource
        // reaction mirrors the descriptor-route sequence load_scene runs (NOT the
        // node-ref bridge that set_node_scene_source uses): re-resolve the
        // descriptor into a Scene, compile, then re-graft + rebuild.
        apply_scene_change(SceneChange::glb_source());
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
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        if (!node || !node->glb_scene_source) {
            ctx_.logger.warn(
                "set_node_glb_base_style: no-op (node '" + node_id
                + "' has no GLB scene source)");
            return false;
        }

        node->glb_scene_source->base_style = style;
        document_.dirty() = true;
        apply_scene_change(SceneChange::glb_source());
        return true;
    }

    bool WozzitsApp_v1::set_node_glb_mesh_style(
        const wz::scene::AuthoredEntityId& node_id,
        uint32_t mesh_index,
        const wz::engine::assets::MeshRenderStyleData& style)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
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

        document_.dirty() = true;
        apply_scene_change(SceneChange::glb_source());
        return true;
    }

    bool WozzitsApp_v1::clear_node_glb_mesh_style(
        const wz::scene::AuthoredEntityId& node_id,
        uint32_t mesh_index)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
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
        const bool changed = overrides.size() != before;
        if (changed) {
            document_.dirty() = true;
        }
        apply_scene_change(
            changed ? SceneChange::glb_source() : SceneChange::none());
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
            wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        if (!host) {
            ctx_.logger.warn(
                "flatten_scene_source: no-op (node '" + node_id + "' missing)");
            return false;
        }

        // Resolve the host's scene source: prefer the cached resolved key, else
        // bridge from the authored node id against the bound graph.
        if (!host->scene_source && host->scene_source_node_id) {
            wz::engine::assets::bridge_scene_source_keys(
                document_.nodes(), graph_draft_);
            host = wz::engine::assets::find_scene_node(document_.nodes(), node_id);
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
        if (!document_.grafted_ids().empty()) {
            std::unordered_set<std::string> stale;
            for (const auto& id : document_.grafted_ids()) {
                if (id.rfind(prefix, 0) == 0) {
                    stale.insert(id);
                }
            }
            if (!stale.empty()) {
                document_.nodes().erase(
                    std::remove_if(
                        document_.nodes().begin(),
                        document_.nodes().end(),
                        [&stale](
                            const wz::engine::assets::SceneNodeAsset& n) {
                            return stale.count(n.id) != 0;
                        }),
                    document_.nodes().end());
                document_.grafted_ids().erase(
                    std::remove_if(
                        document_.grafted_ids().begin(),
                        document_.grafted_ids().end(),
                        [&stale](const wz::scene::AuthoredEntityId& id) {
                            return stale.count(id) != 0;
                        }),
                    document_.grafted_ids().end());
            }
            host = wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        }

        // Expand persistently: the children become real authored nodes (NOT
        // tracked in document_.grafted_ids(), so they persist + save), and the host's
        // scene-source reference is dropped — the expansion is now the content.
        // Snapshot the host BEFORE the appends (push_back may reallocate
        // document_.nodes() and invalidate `host`), then re-find it to detach.
        const wz::engine::assets::SceneNodeAsset host_snapshot = *host;
        std::vector<wz::engine::assets::SceneNodeAsset> children =
            wz::engine::assets::expand_scene_source_children(host_snapshot, *sub);
        const std::size_t count = children.size();
        for (wz::engine::assets::SceneNodeAsset& child : children) {
            document_.nodes().push_back(std::move(child));
        }
        host = wz::engine::assets::find_scene_node(document_.nodes(), node_id);
        if (host) {
            wz::engine::assets::detach_scene_source(*host);
        }

        document_.dirty() = true;
        // The expansion changed the entity set; re-materialize unconditionally.
        apply_scene_change(SceneChange::runtime_rebuild());
        ctx_.logger.info(
            "flatten_scene_source: expanded " + std::to_string(count)
            + " node(s) under '" + node_id + "' (scene source dropped)");
        return true;
    }

    // The pure-document query accessors delegate to document_ (their logic moved
    // to SceneDocument, #258 avenue-2 stage 2). WozzitsApp_v1 keeps the public
    // methods as its ABI surface; the document IS the queryable editing model.
    bool WozzitsApp_v1::node_has_component(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& kind) const
    {
        return document_.node_has_component(node_id, kind);
    }

    std::size_t WozzitsApp_v1::child_node_count(
        const wz::scene::AuthoredEntityId& parent_id) const
    {
        return document_.child_node_count(parent_id);
    }

    std::optional<wz::asset::AssetGraphDraftNodeId>
    WozzitsApp_v1::node_renderable_asset_node_id(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        return document_.node_renderable_asset_node_id(node_id);
    }

    std::optional<wz::asset::AssetGraphDraftNodeId>
    WozzitsApp_v1::node_scene_source_node_id(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        return document_.node_scene_source_node_id(node_id);
    }

    bool WozzitsApp_v1::node_has_glb_scene_source(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        return document_.node_has_glb_scene_source(node_id);
    }

    const wz::engine::assets::SceneGLBSceneSource*
    WozzitsApp_v1::node_glb_scene_source(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        return document_.node_glb_scene_source(node_id);
    }

    const wz::engine::assets::SceneCollisionAsset*
    WozzitsApp_v1::node_collision(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        return document_.node_collision(node_id);
    }

    const wz::engine::assets::SceneMotionAsset*
    WozzitsApp_v1::node_motion(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        return document_.node_motion(node_id);
    }

    std::vector<wz::engine::assets::SceneNodeAsset>
    WozzitsApp_v1::grafted_scene_nodes() const
    {
        return document_.grafted_nodes();
    }

    std::vector<wz::engine::assets::SceneNodeAsset>
    WozzitsApp_v1::authored_scene_nodes() const
    {
        // Same filter as save_scene: drop runtime-only grafted (#213) + "spawn:"
        // prefab-instance nodes, leaving the authored scene the editor edits.
        const std::unordered_set<std::string> grafted(
            document_.grafted_ids().begin(), document_.grafted_ids().end());
        std::vector<wz::engine::assets::SceneNodeAsset> authored;
        authored.reserve(document_.nodes().size());
        for (const wz::engine::assets::SceneNodeAsset& n : document_.nodes()) {
            if (grafted.count(n.id) != 0 || n.id.rfind("spawn:", 0) == 0) {
                continue;
            }
            authored.push_back(n);
            // #221: report the sim-current pose. With a live behavior scene the
            // node's transform is derived from the polytree (the old write-back
            // used to keep document_.nodes() current); with no sim this is the stored
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

    void WozzitsApp_v1::flush_frame_profile_csv()
    {
        if (!ctx_.assets || frame_profile_.empty()) {
            return;
        }

        // Mint a wall-clock run tag ONCE per process so each play session writes
        // its OWN frame_profile_<tag>.csv. Successive play/stop cycles are separate
        // host processes that all flushed one fixed filename before, so a later
        // (e.g. no-spawn) run silently clobbered an earlier spawn run (#252).
        if (frame_profile_run_tag_.empty()) {
            const std::time_t now = std::time(nullptr);
            std::tm lt{};
#ifdef _WIN32
            localtime_s(&lt, &now);
#else
            localtime_r(&now, &lt);
#endif
            char stamp[24] = {};
            std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &lt);
            frame_profile_run_tag_ = stamp;
        }

        // Rows = frames, columns = the recorded metrics. The schema is
        // app-specific (the #252 structural-work metrics); the shared engine helper
        // owns the data_table -> csv_export -> file plumbing. Runs at save/shutdown.
        wz::engine::assets::DataTableData table;
        table.columns.push_back({ .name = "frame" });
        table.columns.push_back({ .name = "dt_ms" });
        table.columns.push_back({ .name = "sim_ms" });
        table.columns.push_back({ .name = "scene_nodes" });
        table.columns.push_back({ .name = "rematerialize" });
        table.columns.push_back({ .name = "rebuild_behavior_scene" });
        table.columns.push_back({ .name = "remat_callers" });
        table.rows.reserve(frame_profile_.size());
        for (const FrameProfileSample& s : frame_profile_) {
            table.rows.push_back({ .cells = {
                std::to_string(s.frame),
                std::to_string(s.dt_ms),
                std::to_string(s.sim_ms),
                std::to_string(s.scene_nodes),
                std::to_string(s.rematerialize),
                std::to_string(s.rebuild),
                s.callers,
            } });
        }

        const wz::fs::Path path =
            wz::fs::join(
                ctx_.assets->resource_root(),
                "frame_profile_" + frame_profile_run_tag_ + ".csv");

        using Status = wz::engine::assets::DataTableCsvExportStatus;
        switch (wz::engine::assets::write_data_table_to_csv_file(
                    *ctx_.assets, "profile/frame_profile", std::move(table), path))
        {
        case Status::TableInvalid:
            ctx_.logger.warn("flush_frame_profile_csv: data table invalid");
            break;
        case Status::ExportInvalid:
            ctx_.logger.warn("flush_frame_profile_csv: csv export invalid");
            break;
        case Status::ExportUnresolved:
            ctx_.logger.warn("flush_frame_profile_csv: csv export unresolved");
            break;
        case Status::WriteFailed:
            ctx_.logger.warn("flush_frame_profile_csv: write failed for " + path);
            break;
        case Status::Ok:
            ctx_.logger.info(
                "flush_frame_profile_csv: wrote " + path + " ("
                + std::to_string(frame_profile_.size()) + " frames)");
            break;
        }
    }

    void WozzitsApp_v1::set_frame_profiling_enabled(bool enabled)
    {
        if (enabled == frame_profiling_enabled_) {
            return;
        }
        if (enabled) {
            // Start a fresh capture. The run tag is minted at flush time, so a
            // cleared tag + empty buffer means this on->off session writes its
            // OWN frame_profile_<tag>.csv.
            frame_profile_.clear();
            frame_profile_run_tag_.clear();
            frame_profiling_enabled_ = true;
            ctx_.logger.info("frame profiling: enabled");
        }
        else {
            // Flush what was captured to its own file, then stop and reset so a
            // later enable starts clean.
            frame_profiling_enabled_ = false;
            flush_frame_profile_csv();
            frame_profile_.clear();
            frame_profile_run_tag_.clear();
            ctx_.logger.info("frame profiling: disabled");
        }
    }

    bool WozzitsApp_v1::save_scene()
    {
        // Per-frame profile (#252): write the accumulated CSV on the way out.
        // save_scene runs at play/editor exit; independent of the scene-dirty
        // early-out below, and a no-op when nothing was recorded.
        flush_frame_profile_csv();

        // The editor viewport's free-fly camera pose is unsaved state independent
        // of scene edits; persist it alongside authored changes. Standalone play
        // never authors the editor camera (prefer_scene_camera_).
        const bool want_editor_camera = !view_.prefer_scene_camera();
        if (!document_.dirty()
            && !(want_editor_camera && view_.editor_camera_dirty())) {
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
                document_.grafted_ids().begin(), document_.grafted_ids().end());
            persisted_nodes.reserve(document_.nodes().size());
            for (const wz::engine::assets::SceneNodeAsset& n : document_.nodes()) {
                if (grafted.count(n.id) != 0
                    || n.id.rfind("spawn:", 0) == 0) {
                    continue;
                }
                persisted_nodes.push_back(n);
                // #221: derive-on-save. The per-frame write-back that used to keep
                // document_.nodes() transforms sim-current is gone, so decompose the
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
            // A camera-only save (scene not dirty) leaves the authored nodes
            // untouched so panning the viewport never churns the node array.
            if (document_.dirty()) {
                wz::engine::assets::set_scene_document_nodes(
                    document, persisted_nodes);
            }
        }
        else {
            // No readable source — emit a fresh scene document from the nodes.
            wz::engine::assets::SceneAssetData snapshot;
            snapshot.nodes = persisted_nodes;
            document =
                wz::engine::assets::export_scene_to_json_document(snapshot);
        }

        // Persist the editor viewport camera (editor only). Upserts just the
        // scene_editor_metadata.camera block, leaving nodes + other data intact.
        if (want_editor_camera && document.root) {
            const wz::bench::FlyingCamera& camera = view_.free_fly_camera();
            wz::engine::assets::SceneEditorCameraMetadata meta;
            meta.position[0] = camera.x;
            meta.position[1] = camera.y;
            meta.position[2] = camera.z;
            meta.orientation[0] = camera.orientation.x;
            meta.orientation[1] = camera.orientation.y;
            meta.orientation[2] = camera.orientation.z;
            meta.orientation[3] = camera.orientation.w;
            meta.move_speed       = camera.move_speed;
            meta.look_speed       = camera.look_speed;
            meta.boost_multiplier = camera.boost_multiplier;
            meta.roll_speed       = camera.roll_speed;
            wz::engine::assets::set_scene_document_editor_camera(document, meta);
        }

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return false;
        }
        out << wz::json::serialize_json(document);
        if (!out.good()) {
            return false;
        }

        document_.dirty() = false;
        view_.clear_editor_camera_dirty();
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
                document_.nodes(), root_node_id);
        if (!subtree) {
            ctx_.logger.error(
                "export_subtree_as_scene: root node not found");
            return false;
        }

        // Exclude runtime-grafted instance children (#213), mirroring
        // save_scene: a prefab keeps a scene_source host's reference but not the
        // sub-tree it grafts at load (that re-imports from the reference). The
        // host node itself (with its scene_source) stays if it is in the subtree.
        if (!document_.grafted_ids().empty()) {
            const std::unordered_set<std::string> grafted(
                document_.grafted_ids().begin(), document_.grafted_ids().end());
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
        // moves document_.nodes()'s transforms), so compose from the nodes exactly as
        // the renderer used to.
        if (!behavior_scene_) {
            return wz::engine::rendering::compute_scene_node_world_transforms(
                document_.nodes());
        }

        // Live simulation: the polytree carries the composed world matrices the
        // sim advanced this frame (propagate_all runs in simulation_tick). Read
        // each node's world straight from it, mapped by stable authored id. Seed
        // from the authored composition so any node missing from the runtime map
        // (defensive — the instance mirrors document_.nodes() 1:1, so this should not
        // happen) still gets a sensible transform rather than identity.
        std::vector<wz::math::Mat4> world =
            wz::engine::rendering::compute_scene_node_world_transforms(
                document_.nodes());
        const std::size_t node_count =
            wz::core::graph::node_count(behavior_scene_->storage.polytree);
        for (std::size_t i = 0; i < document_.nodes().size(); ++i) {
            const auto it =
                behavior_scene_->authored_to_runtime.find(document_.nodes()[i].id);
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
        for (std::size_t i = 0; i < document_.nodes().size(); ++i) {
            if (document_.nodes()[i].id == id) {
                return scene_world_transforms()[i];
            }
        }
        return std::nullopt;
    }

    const wz::engine::assets::AtmosphereData*
    WozzitsApp_v1::resolve_frame_atmosphere()
    {
        if (!ctx_.assets) {
            return nullptr;
        }

        // Prefer the FrameEnvironment: the single connected producer. Its
        // atmosphere role feeds the same view-frequency fog path a standalone
        // Atmosphere did. A scene that authors an Environment (even one whose
        // atmosphere role is empty) takes this branch; the standalone path below
        // is the back-compat fallback for scenes authored before FrameEnvironment.
        const wz::engine::assets::SceneFrameEnvironment env =
            wz::engine::assets::resolve_scene_frame_environment(
                document_.nodes(), *ctx_.assets);

        // Duplicate-environment warning, latched by id so this per-frame path
        // warns ONCE and self-heals when the authoring is fixed.
        {
            const wz::scene::AuthoredEntityId env_duplicate_id =
                env.duplicate ? env.duplicate->id
                              : wz::scene::AuthoredEntityId{};
            if (env_duplicate_id != environment_duplicate_warned_for_) {
                environment_duplicate_warned_for_ = env_duplicate_id;
                if (env.duplicate) {
                    ctx_.logger.warn(
                        "scene authors more than one Environment: using '"
                        + env.source->id + "', ignoring '" + env.duplicate->id
                        + "'. The frame environment is global state, so a second"
                        " one is an authoring error rather than a blend.");
                }
            }
        }

        if (env.source) {
            // A FrameEnvironment is authored. Its atmosphere role is the frame's
            // fog; an empty role means "environment authored, no fog" -- NOT a
            // reason to fall through to a legacy standalone atmosphere. An
            // unresolved bundle (mid-edit) likewise yields no fog this frame.
            if (!env.environment) {
                return nullptr;
            }
            const wz::asset::AssetKey atmosphere_key =
                env.environment->atmosphere;
            if (atmosphere_key == wz::asset::AssetKey{}) {
                return nullptr;
            }
            return ctx_.assets->atmospheres().get_atmosphere_data(
                ctx_.assets->atmospheres().find_atmosphere(
                    wz::engine::assets::AtmosphereAsset{
                        .output = atmosphere_key }));
        }

        // Back-compat: no FrameEnvironment authored -- resolve a standalone
        // Atmosphere component the way scenes did before the environment node.
        const wz::engine::assets::SceneFrameAtmosphere frame =
            wz::engine::assets::resolve_scene_frame_atmosphere(
                document_.nodes(), *ctx_.assets);

        const wz::scene::AuthoredEntityId duplicate_id =
            frame.duplicate ? frame.duplicate->id
                            : wz::scene::AuthoredEntityId{};
        if (duplicate_id != atmosphere_duplicate_warned_for_) {
            atmosphere_duplicate_warned_for_ = duplicate_id;
            if (frame.duplicate) {
                ctx_.logger.warn(
                    "scene authors more than one Atmosphere: using '"
                    + frame.source->id + "', ignoring '" + frame.duplicate->id
                    + "'. Atmosphere is frame-global state, so a second one is"
                    " an authoring error rather than a blend.");
            }
        }

        return frame.atmosphere;
    }

    // The GPU handle backing a resident texture asset, or an invalid handle.
    // One place for the asset-key -> rhi resource -> backend handle hop that
    // both the authored render targets (#287) and the composite target (#281)
    // need.
    wz::gpu::GPUHandle WozzitsApp_v1::texture_gpu_handle(
        const wz::asset::AssetKey& key) const
    {
        if (!ctx_.gpu || key == wz::asset::AssetKey{}) {
            return {};
        }
        const wz::rhi::GpuResourceHandle resource =
            ctx_.gpu->resources.find(wz::rhi::ResourceIdentity{
                wz::engine::assets::rhi_asset_identity(key, "texture"), {} });
        if (!resource.valid()) {
            return {};
        }
        const wz::rhi::GpuResource* res = ctx_.gpu->resources.get(resource);
        if (!res) {
            return {};
        }
        return ctx_.gpu->backend.gpu_handle_for(res->backend);
    }

    // Issue #287: fill every authored render-to-texture target. This is the
    // whole driver -- there is no per-target code, and adding a second target
    // to a project is authoring, not a rebuild.
    bool WozzitsApp_v1::render_authored_render_targets(
        const wz::engine::rendering::AuthoredRenderTargets& authored,
        const std::vector<wz::math::Mat4>& world_transforms)
    {
        if (authored.targets.empty() || !ctx_.assets) {
            return true;
        }

        bool ok = true;
        for (const wz::engine::rendering::AuthoredRenderTarget& target :
             authored.targets)
        {
            const std::string& source_id =
                document_.nodes()[target.source_index].id;

            const wz::gpu::GPUHandle handle = texture_gpu_handle(target.texture);
            if (!handle.valid()) {
                // Once per source, not per frame: a target that never became
                // resident is an authoring mistake worth naming, and naming it
                // 60 times a second would bury everything else.
                if (warned_render_targets_.insert(source_id).second) {
                    ctx_.logger.warn(
                        "render_to_texture: node '" + source_id
                        + "' target texture is not resident as a render target "
                          "(nothing is drawn into it)");
                }
                continue;
            }

            std::vector<wz::engine::assets::SceneNodeAsset> nodes;
            std::vector<wz::math::Mat4> worlds;
            nodes.reserve(target.node_indices.size());
            worlds.reserve(target.node_indices.size());
            for (const std::size_t i : target.node_indices) {
                nodes.push_back(document_.nodes()[i]);
                worlds.push_back(
                    i < world_transforms.size()
                        ? world_transforms[i]
                        : wz::math::Mat4::identity());
            }
            if (nodes.empty()) {
                continue;
            }

            // The frame's view, exactly as the main pass sees it. A per-target
            // authored camera is the obvious next dial; screen-space looks (a
            // puppet, a HUD) ignore it entirely, and a world subtree drawn into
            // a texture reads as "what the camera sees, off-screen".
            ok = renderer_.render_scene(
                     nodes, *ctx_.assets,
                     view_.active_view().view_projection,
                     view_.active_view().world_position,
                     worlds, resolve_frame_atmosphere(), handle)
                && ok;
        }
        return ok;
    }

    bool WozzitsApp_v1::render_scene()
    {
        if (!ctx_.assets) {
            return true;
        }
        // The single active view is kept current by materialize_active_view() each
        // simulation_tick. render_scene just reads it -- no branch, no scene-tree
        // lookup, no fallback. world_position drives the clipmap lattice snap and
        // tracks whichever camera (free-fly or scene) is active.
        //
        // #221: hand the renderer the world transforms from the single source of
        // truth (the live polytree when simulating, the authored composition
        // otherwise) so the drawn pose is the sim-current one -- the per-frame
        // Mat4->TRS->Mat4 write-back into document_.nodes() is gone.
        const std::vector<wz::math::Mat4> world_transforms =
            scene_world_transforms();
        // The frame's global fog, resolved from the scene-authored Atmosphere
        // asset (6c51cf5). nullptr -- no node authors one, the one that does is
        // switched off, or its key has not resolved -- means "no fog"; the camera
        // still reaches the view constants either way.
        // #287: authored render-to-texture sources fill their targets BEFORE the
        // main pass, so a surface sampling one shows this frame's contents
        // rather than last frame's.
        const wz::engine::rendering::AuthoredRenderTargets authored =
            wz::engine::rendering::collect_authored_render_targets(
                document_.nodes());
        if (!render_authored_render_targets(authored, world_transforms)) {
            return false;
        }

        // Which nodes the main pass SKIPS: exactly those an authored target
        // claims exclusively (#287). This used to also hard-code "and puppets,
        // while the showcase flag is on", which meant a puppet in a scene with
        // no card was invisible in BOTH passes. Now the exclusion is authored,
        // so a puppet with no render_to_texture simply draws where it is.
        // Filter nodes + world transforms in lockstep; they are index-aligned.
        const std::vector<bool>& skip = authored.excluded_from_scene;
        bool any_skipped = false;
        for (const bool skipped : skip) {
            any_skipped = any_skipped || skipped;
        }

        if (!any_skipped) {
            return renderer_.render_scene(
                document_.nodes(), *ctx_.assets,
                view_.active_view().view_projection,
                view_.active_view().world_position, world_transforms,
                resolve_frame_atmosphere());
        }

        std::vector<wz::engine::assets::SceneNodeAsset> main_nodes;
        std::vector<wz::math::Mat4> main_transforms;
        main_nodes.reserve(document_.nodes().size());
        main_transforms.reserve(world_transforms.size());
        for (std::size_t i = 0; i < document_.nodes().size(); ++i) {
            if (skip[i]) {
                continue;
            }
            main_nodes.push_back(document_.nodes()[i]);
            if (i < world_transforms.size()) {
                main_transforms.push_back(world_transforms[i]);
            }
        }
        return renderer_.render_scene(
            main_nodes, *ctx_.assets, view_.active_view().view_projection,
            view_.active_view().world_position, main_transforms,
            resolve_frame_atmosphere());
    }

    // The composited material target (#281): the render-target texture a scene
    // renderable binds at material_albedo. Found by ASKING THE RENDERABLES what
    // they bind rather than by scanning for the schema -- the binding is the
    // source of truth for which texture the surface actually samples, so the
    // compositor and the shader can never disagree about the target.
    // Returns an invalid handle when the scene has no such material.
    wz::gpu::GPUHandle WozzitsApp_v1::material_composite_target() const
    {
        if (!ctx_.assets || !ctx_.gpu) {
            return {};
        }
        for (const wz::engine::assets::SceneNodeAsset& node :
             document_.nodes())
        {
            if (!node.renderable_asset.has_value()) {
                continue;
            }
            const wz::engine::assets::RhiRenderableRecipe* recipe =
                ctx_.assets->renderables().get_rhi_renderable_recipe(
                    wz::engine::assets::RenderableAsset{
                        .output = *node.renderable_asset });
            if (!recipe) {
                continue;
            }
            for (const wz::engine::assets::RhiRenderableBinding& binding :
                 recipe->bindings)
            {
                if (binding.semantic != "material_albedo") {
                    continue;
                }
                if (const wz::gpu::GPUHandle handle =
                        texture_gpu_handle(binding.key);
                    handle.valid())
                {
                    return handle;
                }
            }
        }
        return {};
    }

    bool WozzitsApp_v1::render_puppet_showcase()
    {
        if (!puppet_card_showcase_ || !ctx_.assets) {
            return true;
        }

        // The card's texture is an AUTHORED render-to-texture target (#287): the
        // scene node carries a render_to_texture component, and render_scene
        // already filled that texture this frame. This used to be a C++ scan for
        // "nodes whose recipe carries a puppet_key" plus a private offscreen
        // texture and a bespoke render_scene call -- none of which a user could
        // point at different art without a rebuild.
        //
        // The anchor is that same node's WORLD transform: the card lives wherever
        // the node sits in the world. scene_world_transforms() is index-aligned
        // with document_.nodes(); the art's placement INTO the texture is
        // target-fit by the renderer, so the node transform only positions the
        // card.
        const std::vector<wz::math::Mat4> world_transforms =
            scene_world_transforms();
        wz::asset::AssetKey card_texture{};
        wz::math::Mat4 card_anchor = wz::math::Mat4::identity();
        for (std::size_t i = 0; i < document_.nodes().size(); ++i) {
            const std::optional<wz::engine::assets::SceneRenderToTextureAsset>&
                source = document_.nodes()[i].render_to_texture;
            if (!source || !source->enabled
                || source->target == wz::asset::AssetKey{})
            {
                continue;
            }
            card_texture = source->target;
            if (i < world_transforms.size()) {
                card_anchor = world_transforms[i];
            }
            break;
        }
        if (card_texture == wz::asset::AssetKey{}) {
            return true;  // no authored source -- nothing to show on a card
        }

        const wz::gpu::GPUHandle card_rtt = texture_gpu_handle(card_texture);
        if (!card_rtt.valid()) {
            // render_authored_render_targets already warned once about this.
            return true;
        }

        // Composite the puppet into the scene's material texture (#281): clear it
        // to the sphere's base colour, then place the puppet RTT over it. The
        // layer transform IS the "where the art sits on the material" control --
        // centre_uv moves the puppet across the surface, half_size_uv scales it.
        // The composite is a recorded pass, not a synchronous flush, so running
        // it per frame costs a pass rather than a stall; gating it on "the
        // puppet actually changed" is still the obvious refinement.
        if (const wz::gpu::GPUHandle material = material_composite_target();
            material.valid())
        {
            const float base_color[4] = { 0.62f, 0.62f, 0.65f, 1.0f };
            wz::gpu::dx12::internal::TextureCompositeLayer layer{};
            layer.texture = card_rtt;
            layer.center_uv[0] = 0.5f;
            layer.center_uv[1] = 0.5f;
            layer.half_size_uv[0] = 0.35f;
            layer.half_size_uv[1] = 0.35f;
            layer.opacity = 1.0f;
            wz::gpu::dx12::internal::composite_texture_layers_dx12(
                ctx_.device, material, base_color, &layer, 1);
        }

        // Advance the idle spin (a slow turn; frame-paced -- this is a showcase).
        puppet_card_angle_ += 0.02f;

        // World-anchored card: MVP = view_projection * node_world * spin * scale.
        // The card lives at the puppet node's scene-graph world transform, spins
        // about the node's local Y (so it reads as a surface turning in the world),
        // and is viewed through the scene camera -- move/rotate/scale the node in
        // the editor and the card follows. The quad is +/-1 in its local XY plane,
        // so half_size sets its half-extent in world units. Drawn as a world surface
        // (premultiplied-alpha composite so the puppet floats without a black card;
        // depth-tested so nearer scene geometry occludes it).
        const float half_size = 40.0f;  // world-space half-extent of the card
        const float angle = puppet_card_angle_;
        const wz::math::Quaternion spin_q{
            0.0f, std::sin(angle * 0.5f), 0.0f, std::cos(angle * 0.5f) };
        const wz::math::Mat4 card_local = wz::math::mul(
            wz::math::rotation(spin_q),
            wz::math::scale(wz::math::Vec3{ half_size, half_size, half_size }));
        const wz::math::Mat4 card_model = wz::math::mul(card_anchor, card_local);
        const wz::math::Mat4 mvp_mat = wz::math::mul(
            view_.active_view().view_projection, card_model);
        wz::gpu::dx12::internal::draw_textured_quad_dx12(
            ctx_.device, card_rtt, mvp_mat.m,
            wz::gpu::dx12::internal::TexturedQuadMode::WorldSurface);
        return true;
    }

    void WozzitsApp_v1::set_prefer_scene_camera(bool prefer)
    {
        view_.set_prefer_scene_camera(prefer);

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
        if (!view_.prefer_scene_camera())
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

        // Fresh scene: forget which sources were auto-played (a reloaded scene may
        // reuse client ids) so start_spawned_audio() tracks only this scene's.
        auto_played_clients_.clear();

        // Reset spatialization velocity tracking so a new scene starts with no
        // stale prev positions (the first tick then skips Doppler).
        audio_spatialization_.clear();

        const wz::engine::audio::ScenePlaybackReport report =
            wz::engine::audio::play_scene_audio_sources(
                *ctx_.assets, *behavior_scene_, audio_runtime_.scheduler(),
                grain_desc_store_, &auto_played_clients_);

        ctx_.logger.info(
            "scene audio: played " + std::to_string(report.played)
            + " source(s), skipped " + std::to_string(report.skipped_disabled)
            + " disabled / " + std::to_string(report.skipped_unresolved)
            + " unresolved");
    }

    void WozzitsApp_v1::start_spawned_audio()
    {
        // Only play mode has an open device + the auto-play policy; the editor is
        // silent and never spawns into a running mixer.
        if (!view_.prefer_scene_camera())
            return;
        if (!ctx_.assets || !behavior_scene_)
            return;
        if (!audio_runtime_.running())
            return;  // device never came up — nothing to add to

        // Re-run the auto-play pass over the (rebuilt) instance, skipping every
        // client id already started (auto_played_clients_). Only the freshly
        // spawned subtree's auto_play sources are new, so only they fire; the
        // ambient bed and earlier spawns are left untouched. grain_desc_store_ is
        // NOT cleared (live clouds still reference their descs) — a new cloud just
        // appends a fresh desc.
        const wz::engine::audio::ScenePlaybackReport report =
            wz::engine::audio::play_scene_audio_sources(
                *ctx_.assets, *behavior_scene_, audio_runtime_.scheduler(),
                grain_desc_store_, &auto_played_clients_);

        if (report.played > 0) {
            ctx_.logger.info(
                "spawn audio: started " + std::to_string(report.played)
                + " new source(s)");
        }
    }

    void WozzitsApp_v1::materialize_active_view()
    {
        // The app's REACTION half of the view seam (#258): resolve the selected
        // scene camera's live world transform from the behavior scene's polytree,
        // then hand it to view_, which owns the camera math + source policy. The
        // free-fly source needs no scene data (view_ ignores it there).
        const std::size_t node_count = behavior_scene_
            ? wz::core::graph::node_count(behavior_scene_->storage.polytree)
            : 0;

        // Read the selected camera node's already-maintained world transform
        // straight from the live scene graph through its handle. A camera parented
        // under a moving node (e.g. a tank) follows it because the graph keeps
        // that node's world matrix current. std::nullopt when the handle can't be
        // resolved this frame (e.g. mid-rebuild) -> view_ holds the previous view
        // rather than dropping to free-fly, so a transient invalid handle cannot
        // flip the camera. Only read in the Scene source (matches the original).
        std::optional<wz::math::Mat4> scene_camera_world;
        const wz::scene::RuntimeEntityId camera_entity =
            view_.active_camera_entity();
        if (view_.scene_source_active() && behavior_scene_
            && camera_entity != wz::scene::INVALID_RUNTIME_ENTITY
            && camera_entity < node_count)
        {
            scene_camera_world = wz::core::graph::node_data(
                behavior_scene_->storage.polytree, camera_entity).world;
        }

        view_.update_active_view(
            scene_camera_world, node_count, behavior_scene_.has_value(),
            ctx_.logger);
    }

    std::size_t WozzitsApp_v1::resident_gpu_resource_count() const
    {
        return renderer_.resident_gpu_resource_count();
    }

    std::size_t WozzitsApp_v1::resolved_renderable_node_count() const
    {
        std::size_t count = 0;
        for (const wz::engine::assets::SceneNodeAsset& node : document_.nodes()) {
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
