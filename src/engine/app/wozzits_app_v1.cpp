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
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/renderable/render_binding_sources.h>
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
        // FNV-1a/32 over a renderable-constant name. Matches
        // wz_renderable_param_hash (behavior-side) bit-for-bit so a
        // SET_RENDERABLE_PARAM command's hash resolves to the declared field of
        // the same name (#232). Same algorithm as prefab_name_hash — kept a
        // distinct function for a semantically distinct call site.
        uint32_t renderable_param_name_hash(std::string_view name) noexcept
        {
            uint32_t h = 2166136261u;
            for (const unsigned char c : name) {
                h ^= static_cast<uint32_t>(c);
                h *= 16777619u;
            }
            return h;
        }

        // Effective (inherited) "active" per authored node id (#252 live axis): a
        // node is live only if it AND every ancestor is `active`, so parking a node
        // parks its whole subtree (a spawn host parks its grafted children). Mirrors
        // the renderer's effective-visibility parent-walk (rhi_scene_renderer.cpp),
        // keyed on `active` instead of `visible`. Cycle/dangling-parent safe: an
        // unresolved parent falls back to the node's own `active` (never spuriously
        // parked). Returns id -> 1(live)/0(parked) so the caller projects it onto
        // runtime entities via runtime_to_authored.
        std::unordered_map<std::string, std::uint8_t>
        compute_scene_node_effective_active(
            const std::vector<wz::engine::assets::SceneNodeAsset>& nodes)
        {
            const std::size_t n = nodes.size();

            std::unordered_map<std::string, std::size_t> index_by_id;
            index_by_id.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                index_by_id.emplace(nodes[i].id, i);
            }

            std::vector<std::size_t> parent(n, n);
            for (std::size_t i = 0; i < n; ++i) {
                if (!nodes[i].parent_id.has_value()) {
                    continue;
                }
                const auto it = index_by_id.find(*nodes[i].parent_id);
                if (it != index_by_id.end() && it->second != i) {
                    parent[i] = it->second;
                }
            }

            // effective = node.active AND parent.effective, resolved parents-first
            // (cycle-safe chain unwind, identical to the visibility helper).
            std::vector<std::uint8_t> effective(n, 1u);
            std::vector<std::uint8_t> state(n, 0u);
            std::vector<std::size_t> chain;
            chain.reserve(n);
            for (std::size_t start = 0; start < n; ++start) {
                if (state[start] != 0u) {
                    continue;
                }
                chain.clear();
                std::size_t cur = start;
                while (cur != n && state[cur] == 0u) {
                    state[cur] = 1u;
                    chain.push_back(cur);
                    cur = parent[cur];
                }
                for (std::size_t k = chain.size(); k-- > 0;) {
                    const std::size_t i = chain[k];
                    const std::size_t p = parent[i];
                    const bool parent_active = (p == n || state[p] != 2u)
                        ? true
                        : effective[p] != 0u;
                    effective[i] = (nodes[i].active && parent_active) ? 1u : 0u;
                    state[i] = 2u;
                }
            }

            std::unordered_map<std::string, std::uint8_t> by_id;
            by_id.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                by_id.emplace(nodes[i].id, effective[i]);
            }
            return by_id;
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

        // Editor viewport only (standalone play uses the authored scene camera):
        // seed the free-fly camera from the scene file's editor metadata so the
        // project reopens looking from where it was left. The metadata is read
        // straight from the raw scene file -- it is intentionally NOT part of the
        // SceneAssetData game-scene model -- and an absent/partial block leaves the
        // camera at its current pose (its default on first load).
        if (!prefer_scene_camera_) {
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
                        camera_.x = meta->position[0];
                        camera_.y = meta->position[1];
                        camera_.z = meta->position[2];
                        camera_.orientation = { meta->orientation[0],
                            meta->orientation[1], meta->orientation[2],
                            meta->orientation[3] };
                        camera_.move_speed       = meta->move_speed;
                        camera_.look_speed       = meta->look_speed;
                        camera_.boost_multiplier = meta->boost_multiplier;
                        camera_.roll_speed       = meta->roll_speed;
                        ctx_.logger.info(
                            "load_scene: restored editor viewport camera");
                    }
                }
            }
        }
        editor_camera_dirty_ = false;

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
        if (drive_camera) {
            const wz::bench::FlyingCamera before = camera_;
            wz::bench::update_flying_camera(camera_, input, dt);
            // Editor viewport only: a moved free-fly camera is unsaved viewport
            // state, so flag it for save_scene (standalone play does not author
            // the editor camera). Compare pose so a hold-still frame stays clean.
            if (!prefer_scene_camera_
                && (camera_.x != before.x || camera_.y != before.y
                    || camera_.z != before.z
                    || camera_.orientation.x != before.orientation.x
                    || camera_.orientation.y != before.orientation.y
                    || camera_.orientation.z != before.orientation.z
                    || camera_.orientation.w != before.orientation.w))
            {
                editor_camera_dirty_ = true;
            }
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
                .scene_nodes = scene_nodes_.size(),
                .rematerialize = rematerialize_count_this_frame_,
                .rebuild = rebuild_scene_count_this_frame_,
                .callers = remat_callers_this_frame_,
            });
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
                for (const auto& n : scene_nodes_) {
                    if (n.id == id) {
                        return &n;
                    }
                }
                return nullptr;
            };

        std::size_t assembled = 0;
        for (wz::engine::assets::SceneNodeAsset& node : scene_nodes_) {
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

    void WozzitsApp_v1::rebuild_behavior_scene()
    {
        ++rebuild_scene_count_this_frame_;
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

        // Cache whether any behavior subscribes to self.activated, so the per-frame
        // rising-edge scan in dispatch_scene_behaviors is skipped entirely when
        // nothing listens (#252 pooling -- zero cost until a pool manager wires it).
        has_self_activated_subscriber_ =
            wz::engine::behavior::scene_has_event_subscriber(
                *behavior_scene_, registry_, WZ_EVENT_SELF_ACTIVATED);

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
                // A prewarm behavior submits its pool on self.start (#252 pooling);
                // wire the sink so those submits are captured (drained next frame).
                .spawn_requests = &spawn_identity_buffer_,
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

    void WozzitsApp_v1::apply_all_behavior_commands(
        const std::vector<wz::engine::behavior::BehaviorCommand>& commands,
        std::vector<wz::scene::RuntimeEntityId>&                  changed_entities,
        std::vector<DeferredSpawnRequest>&                        out_spawn_requests)
    {
        // Precondition: a live behavior scene. The main-frame caller already holds
        // this (dispatch_scene_behaviors returns early otherwise); guard anyway so
        // the one applier is safe for any future dispatch site to call.
        if (!behavior_scene_) {
            return;
        }

        // Apply the produced command buffer, exactly as game_app's
        // apply_behavior_commands job: transform/velocity commands mutate the
        // instance polytree, then world Y etc. settle on the next propagate.
        (void)wz::engine::behavior::apply_behavior_commands(
            *behavior_scene_,
            commands,
            &changed_entities);

        // Audio behavior commands (item 9): play/stop/set-gain the addressed
        // entity's AudioSource through the realtime scheduler. Only while the
        // audio runtime is live (play mode + a device); otherwise dropped (no
        // device => no sound). apply_behavior_commands ignores these kinds —
        // they don't mutate the entity, they post to the audio thread.
        if (ctx_.assets && audio_runtime_.running()) {
            namespace ea_audio = wz::engine::audio;
            for (const wz::engine::behavior::BehaviorCommand& command :
                 commands)
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
                                                     : 0.0f),
                        // values[3] = source index (GrainParam::SourceWeight).
                        static_cast<uint8_t>(
                            command.values[3] > 0.0f ? command.values[3]
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

        // SET_RENDERABLE_PARAM (issue #232): write the addressed node's
        // per-instance renderable_constants override, which the next
        // frame's pack merges into the draw packet (the #229 seam). Like
        // the audio verbs this is host-handled (apply_behavior_commands
        // ignores it) and runs in the command pass — it mutates only the
        // authored SceneNodeAsset (no behavior-runtime rebuild, no
        // recompile, no re-key), so it does not renumber the runtime ids
        // the remaining commands address. `entity` (runtime) is resolved to
        // its stable authored id here while behavior_scene_ is current.
        for (const wz::engine::behavior::BehaviorCommand& command :
             commands)
        {
            if (command.kind
                != wz::engine::behavior::BehaviorCommandKind
                    ::SetRenderableParam)
            {
                continue;
            }
            if (command.entity
                >= behavior_scene_->runtime_to_authored.size())
            {
                continue;
            }
            const wz::scene::AuthoredEntityId authored_id =
                behavior_scene_->runtime_to_authored[command.entity];

            uint32_t name_hash = 0u;
            std::memcpy(
                &name_hash, &command.values[0], sizeof(uint32_t));

            // Resolve the name hash to a real field name: prefer the
            // synthesized recipe's declared constants (the full addressable
            // set), then the node's authored overrides (so a look that has
            // not compiled — e.g. no device — is still addressable by an
            // existing override).
            std::string name;
            if (ctx_.assets) {
                if (const std::optional<wz::asset::AssetKey> key =
                        node_renderable_asset(authored_id))
                {
                    if (const wz::engine::assets::RhiRenderableRecipe*
                            recipe =
                                ctx_.assets->renderables()
                                    .get_rhi_renderable_recipe(
                                        wz::engine::assets::RenderableAsset{
                                            .output = *key }))
                    {
                        for (const wz::engine::assets::RhiRenderableConstant&
                                 c : recipe->constants)
                        {
                            if (renderable_param_name_hash(c.name)
                                == name_hash)
                            {
                                name = c.name;
                                break;
                            }
                        }
                    }
                }
            }
            if (name.empty()) {
                if (const wz::engine::assets::SceneNodeAsset* node =
                        wz::engine::assets::find_scene_node(
                            scene_nodes_, authored_id))
                {
                    for (const wz::engine::assets::
                             SceneRenderableConstantOverride& c :
                         node->renderable_constants)
                    {
                        if (renderable_param_name_hash(c.name) == name_hash)
                        {
                            name = c.name;
                            break;
                        }
                    }
                }
            }
            if (name.empty()) {
                ctx_.logger.warn(
                    "behavior set_renderable_param: node '" + authored_id
                    + "' has no declared or overridden constant matching "
                      "the command's name hash (skipped)");
                continue;
            }

            // The command carries only x/y/z; preserve the field's fourth
            // component (w / alpha) from any existing override, else a
            // sensible opaque default so a first color pulse is visible.
            float w = 1.0f;
            if (const std::optional<std::array<float, 4>> existing =
                    node_renderable_constant(authored_id, name))
            {
                w = (*existing)[3];
            }
            const float value[4] = {
                command.values[1],
                command.values[2],
                command.values[3],
                w,
            };
            (void)set_node_renderable_constant(authored_id, name, value);
        }

        // SET_NODE_VISIBLE (issue #250): flip the addressed node's authored
        // `visible` flag. Render-only + host-handled (apply ignores it) — a
        // cheap field write, no behavior rebuild / render re-assemble. The
        // renderer honors visibility hierarchically, so hiding a node hides its
        // whole subtree. Same runtime->authored id resolution as above.
        for (const wz::engine::behavior::BehaviorCommand& command :
             commands)
        {
            if (command.kind
                != wz::engine::behavior::BehaviorCommandKind::SetNodeVisible)
            {
                continue;
            }
            if (command.entity
                >= behavior_scene_->runtime_to_authored.size())
            {
                continue;
            }
            const wz::scene::AuthoredEntityId authored_id =
                behavior_scene_->runtime_to_authored[command.entity];
            if (wz::engine::assets::SceneNodeAsset* node =
                    wz::engine::assets::find_scene_node(
                        scene_nodes_, authored_id))
            {
                node->visible = command.values[0] != 0.0f;
                scene_dirty_ = true;
            }
        }

        // SET_NODE_ACTIVE (issue #252, the "live?" axis): flip the addressed
        // node's authored `active` flag. Host-handled (apply ignores it) — a
        // cheap field write, no behavior rebuild. The hierarchical effect gates
        // dispatch + collision via the entity_active mask rebuilt each frame in
        // dispatch_scene_behaviors (NOT render, unlike SET_NODE_VISIBLE). Same
        // runtime->authored id resolution as above.
        for (const wz::engine::behavior::BehaviorCommand& command :
             commands)
        {
            if (command.kind
                != wz::engine::behavior::BehaviorCommandKind::SetNodeActive)
            {
                continue;
            }
            if (command.entity
                >= behavior_scene_->runtime_to_authored.size())
            {
                continue;
            }
            const wz::scene::AuthoredEntityId authored_id =
                behavior_scene_->runtime_to_authored[command.entity];
            if (wz::engine::assets::SceneNodeAsset* node =
                    wz::engine::assets::find_scene_node(
                        scene_nodes_, authored_id))
            {
                node->active = command.values[0] != 0.0f;
                scene_dirty_ = true;
            }
        }

        // Per-frame SET_ACTIVE_CAMERA: a behavior (e.g. spawn_player, after the
        // deferred spawn of the player prefab) can switch the runtime camera on ANY
        // frame, not just at scene load. The scene-load path already applies this
        // once; mirror it here. behavior_scene_ is still current (spawns are applied
        // at the frame boundary below), and apply_scene_active_camera caches the
        // authored id, so any same-frame spawn renumbering is re-seated by
        // refresh_active_camera_entity.
        for (const wz::engine::behavior::BehaviorCommand& command :
             commands)
        {
            if (command.kind
                == wz::engine::behavior::BehaviorCommandKind::SetActiveCamera)
            {
                apply_scene_active_camera(command.entity);
            }
        }

        // Collect SPAWN_PREFAB requests (runtime prefab spawning). Resolve the
        // spawner runtime entity -> its STABLE authored id NOW, while
        // behavior_scene_ is still the scene the command was issued against; a
        // later spawn in the drain rebuilds + renumbers the runtime, so a
        // runtime id would go stale. Decode values[0] (the name hash as a float
        // BIT PATTERN, mirroring the audio clip-name trick) and values[1..3]
        // (the offset). Drained by the caller at the frame boundary.
        for (const wz::engine::behavior::BehaviorCommand& command :
             commands)
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
            out_spawn_requests.push_back(DeferredSpawnRequest{
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

    void WozzitsApp_v1::dispatch_scene_behaviors(
        const wz::input::InputState& input, float dt)
    {
        // One behavior tick, as a short sequence of named phases (#256 seam B).
        // Mirrors game_app's job order: build_collision_frame -> [behaviors] ->
        // integrate_motion -> apply_terrain_constraints. Behaviors are OPTIONAL -- a
        // motion-only / constraint-only actor still needs the integrate + constraint
        // phases to stick to its surface -- so only the dispatch phase is gated on
        // has_behaviors; the rest run whenever a runtime scene exists.
        if (!behavior_scene_) {
            return;
        }
        const bool has_behaviors = !behavior_scene_->behaviors.empty();

        // World transforms must be current before dispatch: command application
        // (set_world_translation, motion integration) reads parent world matrices,
        // and behavior transform queries read self/other world. In game_app this is
        // the compile_scene job; here we propagate directly.
        wz::scene::propagate_all(behavior_scene_->storage.polytree);

        // Phase 1: refresh the per-frame "live?" mask and fire the SELF_ACTIVATED
        // rising edges, returning the commands those handlers produced (seeded into
        // the dispatch phase's buffer below, SAME frame).
        const std::vector<wz::engine::behavior::BehaviorCommand>
            activation_commands = compute_active_mask_and_fire_edges();

        // Phase 2: build the collision / proximity / input event tables the dispatch
        // and constraint phases read.
        build_frame_event_tables(input);

        // Per-frame scratch shared across the remaining phases: the transform-changed
        // entities (for the final re-propagate), the deferred-authoring sink, and the
        // frame-boundary prefab spawns collected during the command apply.
        std::vector<wz::scene::RuntimeEntityId> changed_entities;
        wz::engine::behavior::BehaviorAuthoringBuffer authoring;
        std::vector<DeferredSpawnRequest> spawn_requests;

        // Phase 3: run the behaviors + cognition tick and apply the produced command
        // buffer. Behaviors are optional; a scene with none skips straight to motion.
        if (has_behaviors) {
            dispatch_behaviors_and_apply(
                input, dt, activation_commands,
                changed_entities, authoring, spawn_requests);
        }

        // Phase 4: integrate motion + snap terrain constraints, then re-propagate if
        // anything moved.
        integrate_motion_and_constraints(dt, changed_entities);

        // Phases 5-7: frame-boundary drains, safe only now -- each may rebuild the
        // behavior runtime out from under us, AFTER every read of behavior_scene_
        // this tick. Deferred authoring, fire-and-forget prefab spawns, then the
        // spawn-with-identity completions.
        drain_deferred_authoring(authoring);
        drain_prefab_spawns(spawn_requests);
        drain_identity_spawns();
    }

    std::vector<wz::engine::behavior::BehaviorCommand>
    WozzitsApp_v1::compute_active_mask_and_fire_edges()
    {
        // Refresh the "live?" mask (#252): a node is dispatched + collides only if it
        // AND every ancestor is `active`. Recomputed each frame from the authored
        // scene_nodes_ (cheap O(scene)) and projected onto runtime entities via
        // runtime_to_authored, so a park/unpark flip, a reparent, or a spawn is
        // reflected immediately. build_collision_frame + dispatch_behaviors read
        // behavior_scene_->entity_active (empty => all live, so a non-active-aware
        // caller like game_app is unaffected). Orthogonal to `visible`.
        std::vector<wz::engine::behavior::BehaviorCommand> activation_commands;
        const std::unordered_map<std::string, std::uint8_t> effective_active =
            compute_scene_node_effective_active(scene_nodes_);
        const std::size_t node_count =
            wz::core::graph::node_count(behavior_scene_->storage.polytree);
        behavior_scene_->entity_active.assign(node_count, 1u);
        for (std::size_t entity = 0;
             entity < node_count
                 && entity < behavior_scene_->runtime_to_authored.size();
             ++entity)
        {
            const auto it = effective_active.find(
                behavior_scene_->runtime_to_authored[entity]);
            behavior_scene_->entity_active[entity] =
                (it != effective_active.end()) ? it->second : 1u;
        }

        // WZ_EVENT_SELF_ACTIVATED (#252 pooling): fire on the parked -> live rising
        // edge (an external unpark, e.g. a pool acquire) so a reused instance
        // self-resets. Diffed by authored id, since runtime ids renumber on rebuild.
        // Absent-in-prev counts as live, so a node's BIRTH (handled by self.start) is
        // not mistaken for an activation edge. Skipped wholesale unless a subscriber
        // exists AND there is a prior frame to diff.
        if (has_self_activated_subscriber_ && !prev_active_by_id_.empty()) {
            wz::engine::behavior::BehaviorCommandBuffer activated_commands;
            wz::engine::behavior::BehaviorFrameContext activated_ctx{
                .scene = &*behavior_scene_,
                .behavior_state = &behavior_scene_->behavior_state,
                .commands = &activated_commands,
                .spawn_requests = &spawn_identity_buffer_,
                .logger = &ctx_.logger,
                .sim_time = behavior_sim_time_,
            };
            for (const auto& [node_id, current] : effective_active) {
                if (current == 0u) {
                    continue;
                }
                const auto prev_it = prev_active_by_id_.find(node_id);
                const std::uint8_t previous =
                    (prev_it != prev_active_by_id_.end())
                        ? prev_it->second
                        : std::uint8_t{ 1 };
                if (previous != 0u) {
                    continue;  // not a 0 -> 1 rising edge
                }
                const auto rt =
                    behavior_scene_->authored_to_runtime.find(node_id);
                if (rt == behavior_scene_->authored_to_runtime.end()) {
                    continue;
                }
                wz::engine::behavior::dispatch_behavior_event(
                    *behavior_scene_,
                    registry_,
                    activated_ctx,
                    wz::engine::behavior::BehaviorEvent{
                        .kind = WZ_EVENT_SELF_ACTIVATED,
                        .entity = rt->second,
                    });
            }
            // Return the produced commands to the orchestrator, which seeds them into
            // the dispatch phase's command buffer (injected just after the clear,
            // SAME frame) -- a SELF_ACTIVATED self-reset that sets visible / active /
            // spawns / plays audio then gets the full drain via
            // apply_all_behavior_commands rather than being silently dropped by an
            // apply-only pass (#252 audit). No id staleness: the edge pass ran this
            // frame, before the dispatch phase.
            activation_commands.insert(
                activation_commands.end(),
                activated_commands.commands.begin(),
                activated_commands.commands.end());
        }

        prev_active_by_id_ = effective_active;
        return activation_commands;
    }

    void WozzitsApp_v1::build_frame_event_tables(
        const wz::input::InputState& input)
    {
        // Build the collision frame (collision world + terrain constraint surfaces)
        // BEFORE motion/behaviors, exactly as game_app's job_build_collision_frame.
        // apply_terrain_constraints reads frame_storage_.collision to resolve the
        // surface a terrain_constrained Motion actor sticks to. Guard the asset
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
        // Input events (input.* behaviors, e.g. a controller-driven tank): convert
        // this frame's input into routed events so dispatch fires the behavior's
        // on_event. game_app does this in job_build_input_events; the new runtime had
        // left these tables empty, so input-driven behaviors never received events.
        wz::engine::input_events::build_input_event_frame(
            input, *behavior_scene_, frame_storage_.input_events);
    }

    void WozzitsApp_v1::dispatch_behaviors_and_apply(
        const wz::input::InputState& input,
        float dt,
        const std::vector<wz::engine::behavior::BehaviorCommand>& activation_commands,
        std::vector<wz::scene::RuntimeEntityId>& changed_entities,
        wz::engine::behavior::BehaviorAuthoringBuffer& authoring,
        std::vector<DeferredSpawnRequest>& spawn_requests)
    {
        // Build a minimal FrameContext carrying time + input. The collision,
        // proximity and input-event tables were populated by build_frame_event_tables,
        // so the dispatch routes real collision/proximity/input events to behaviors.
        wz::engine::FrameContext frame_context{};
        frame_context.input = input;
        frame_context.frame.interval.start = 0;
        frame_context.frame.interval.end = static_cast<wz::time::Tick>(
            static_cast<double>(dt)
            * static_cast<double>(
                wz::time::TimeSource::ticks_per_second()));
        frame_context.frame.index = behavior_frame_index_++;

        frame_storage_.behavior_commands.clear();
        // Seed with the SELF_ACTIVATED pass's commands (collected earlier THIS frame,
        // before this clear) so a pooled instance's self-reset gets the SAME full
        // drain -- transforms AND host-handled kinds (visible / active / spawn /
        // audio / param) -- as per-frame behaviors, rather than the apply-only that
        // silently dropped every host-handled command it issued (#252 audit).
        if (!activation_commands.empty()) {
            frame_storage_.behavior_commands.commands.insert(
                frame_storage_.behavior_commands.commands.end(),
                activation_commands.begin(),
                activation_commands.end());
        }

        // Advance the monotonic sim clock the self-paced cognition scheduler stamps
        // against (the FrameContext interval is per-frame, not absolute).
        behavior_sim_time_ += static_cast<double>(dt);

        wz::engine::behavior::BehaviorFrameContext behavior_ctx{
            .frame_context = &frame_context,
            .frame_storage = &frame_storage_,
            .scene = &*behavior_scene_,
            .behavior_state = &behavior_scene_->behavior_state,
            .commands = &frame_storage_.behavior_commands,
            .gpu_compute = nullptr,
            .spawn_requests = &spawn_identity_buffer_,
            .authoring = &authoring,
            .logger = &ctx_.logger,
            .sim_time = behavior_sim_time_,
        };
        wz::engine::behavior::dispatch_behaviors(
            *behavior_scene_, registry_, behavior_ctx);

        // Self-paced cognition: fire cognition.tick to agents whose own scheduled
        // wake is due now, APPENDING any actuator commands to the same buffer (applied
        // just below). Not a per-frame call into every agent -- only those due at
        // behavior_sim_time_.
        wz::engine::behavior::dispatch_cognition_tick(
            *behavior_scene_, registry_, behavior_ctx);

        // Apply the produced command buffer through the single converged applier
        // (#256 seam A): the transform/velocity kinds AND every host-handled immediate
        // kind (audio / renderable-param / visible / active / active-camera), plus
        // COLLECT the deferred SPAWN_PREFAB requests into spawn_requests for the
        // frame-boundary drain.
        apply_all_behavior_commands(
            frame_storage_.behavior_commands.commands,
            changed_entities,
            spawn_requests);
    }

    void WozzitsApp_v1::integrate_motion_and_constraints(
        float dt, std::vector<wz::scene::RuntimeEntityId>& changed_entities)
    {
        std::vector<wz::scene::RuntimeEntityId> velocity_changed;
        (void)wz::engine::behavior::integrate_motion(
            *behavior_scene_, dt, &velocity_changed);
        changed_entities.insert(
            changed_entities.end(),
            velocity_changed.begin(),
            velocity_changed.end());

        // Snap terrain_constrained Motion actors to their constraint surface (from
        // frame_storage_.collision built above), exactly as game_app's
        // job_apply_terrain_constraints. Runs after integrate_motion so the constraint
        // corrects the just-integrated position.
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
            // Re-propagate so world matrices (and any next-frame world-space reads)
            // reflect the applied local changes. #221: the polytree is now the single
            // source of truth for render/audio/spawn world transforms (see
            // scene_world_transforms()), so there is no per-frame write-back into
            // scene_nodes_ any more -- scene_nodes_ transforms are derived from the
            // polytree only when actually needed (save + editor read-back). The lossy
            // Mat4->TRS->Mat4 round trip is gone from the frame path.
            wz::scene::propagate_all(behavior_scene_->storage.polytree);
        }
    }

    void WozzitsApp_v1::drain_deferred_authoring(
        const wz::engine::behavior::BehaviorAuthoringBuffer& authoring)
    {
        // Frame-boundary drain of behavior-issued deferred authoring (#204). This runs
        // AFTER the dispatch loop has finished iterating the scene, so mutating it
        // here is not reentrant. Each request goes through the SAME apply method the
        // host's add_child uses (add_child_node) -- the single converged apply path.
        // add_child_node re-materializes the behavior runtime (rebuild_behavior_scene)
        // on success, which is why the drains are among the LAST things the tick does:
        // every read of behavior_scene_ above has already happened, and behavior_scene_
        // may be rebuilt out from under us here safely. Fire-and-forget: no id flows
        // back to the behavior. The parents were resolved to authored ids at enqueue
        // time, so they remain valid even as a prior add in this same drain renumbers
        // runtime ids.
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

        // remove_node drain: same frame-boundary, same single converged apply path the
        // host's remove uses (remove_node, which also rebuilds the behavior runtime on
        // success). Authored-id targets stay valid even as a prior add/remove in this
        // same drain renumbers runtime ids.
        for (const wz::scene::AuthoredEntityId& target :
             authoring.remove_node_targets)
        {
            if (!remove_node(target)) {
                ctx_.logger.warn(
                    "behavior remove_node rejected for node '" + target + "'");
            }
        }

        // set_renderable_asset drain: same frame-boundary, same apply method the host
        // uses (set_node_renderable_asset). This is a cheap field write + dirty flag
        // (no behavior-runtime rebuild, no asset-DAG recompile), so it is
        // order-independent of the structural drains above.
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

        // reparent drain: same frame-boundary, same apply method the host's reparent
        // uses (reparent_node, which rebuilds the behavior runtime on success like
        // add_child/remove). Both ids were resolved to authored ids at enqueue time
        // (an empty new_parent_id = top level), so they stay valid even as a prior
        // structural drain entry renumbers runtime ids.
        for (const wz::engine::behavior::BehaviorReparentRequest& request :
             authoring.reparent_requests)
        {
            if (!reparent_node(request.node_id, request.new_parent_id)) {
                ctx_.logger.warn(
                    "behavior reparent_node rejected for node '"
                    + request.node_id + "'");
            }
        }

        // add/remove-component drains: same frame-boundary, same apply methods the
        // host uses (add_node_component / remove_node_component). These are cheap field
        // edits (no behavior-runtime rebuild, no asset-DAG recompile), so they are
        // order-independent of the structural drains above. The kind was copied into
        // each request at enqueue time (the behavior's transient const char* is long
        // gone by now).
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

    std::optional<wz::asset::AssetKey> WozzitsApp_v1::node_renderable_asset(
        const wz::scene::AuthoredEntityId& id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, id);
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

    bool WozzitsApp_v1::set_node_renderable_binding(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& semantic,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
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

        scene_dirty_ = true;
        // A binding decides whether the assembled renderable is the custom
        // (0x70A) form, so re-assemble like the geometry/program seams.
        rematerialize_render_bindings();
        return true;
    }

    bool WozzitsApp_v1::set_node_renderable_constant(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& name,
        const float* value)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
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
        scene_dirty_ = true;
        if (custom_form_flipped) {
            // Only THIS node's recipe changed (plain pull-mesh <-> custom 0x70A);
            // re-assemble just it, not the whole scene (#253).
            rematerialize_node_render_binding(node->id);
        }
        return true;
    }

    std::optional<std::array<float, 4>>
    WozzitsApp_v1::node_renderable_constant(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& name) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(scene_nodes_, node_id);
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

        // Rows = frames, columns = the recorded metrics. Built as a data_table asset
        // and exported via csv_export -- the same chain the diagnostic tables use
        // (issue #252). Runs at save/shutdown; the commit re-registers the graph,
        // but resolve is cache-hit for the existing scene, so only the two profile
        // nodes do real work.
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

        const wz::engine::assets::DataTableAsset table_asset =
            ctx_.assets->data_tables().create_inline_table(
                { .name = "profile/frame_profile", .table = std::move(table) });
        if (!table_asset.valid()) {
            ctx_.logger.warn("flush_frame_profile_csv: data table invalid");
            return;
        }
        const wz::engine::assets::CSVExportAsset csv_asset =
            ctx_.assets->csv_export().create_csv_export(
                { .name = "profile/frame_profile_csv", .source = table_asset });
        if (!csv_asset.valid()) {
            ctx_.logger.warn("flush_frame_profile_csv: csv export invalid");
            return;
        }

        ctx_.assets->commit();
        (void)ctx_.assets->resolve_all();

        const wz::engine::assets::CSVExportHandle handle =
            ctx_.assets->csv_export().get_export(csv_asset);
        if (!handle.valid()) {
            ctx_.logger.warn("flush_frame_profile_csv: csv export unresolved");
            return;
        }
        const wz::fs::Path path =
            wz::fs::join(
                ctx_.assets->resource_root(),
                "frame_profile_" + frame_profile_run_tag_ + ".csv");
        if (ctx_.assets->csv_export().write_export_to_file(handle, path)
            != wz::fs::FileError::None)
        {
            ctx_.logger.warn("flush_frame_profile_csv: write failed for " + path);
        }
        else {
            ctx_.logger.info(
                "flush_frame_profile_csv: wrote " + path + " ("
                + std::to_string(frame_profile_.size()) + " frames)");
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
        const bool want_editor_camera = !prefer_scene_camera_;
        if (!scene_dirty_ && !(want_editor_camera && editor_camera_dirty_)) {
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
            // A camera-only save (scene not dirty) leaves the authored nodes
            // untouched so panning the viewport never churns the node array.
            if (scene_dirty_) {
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
            wz::engine::assets::SceneEditorCameraMetadata meta;
            meta.position[0] = camera_.x;
            meta.position[1] = camera_.y;
            meta.position[2] = camera_.z;
            meta.orientation[0] = camera_.orientation.x;
            meta.orientation[1] = camera_.orientation.y;
            meta.orientation[2] = camera_.orientation.z;
            meta.orientation[3] = camera_.orientation.w;
            meta.move_speed       = camera_.move_speed;
            meta.look_speed       = camera_.look_speed;
            meta.boost_multiplier = camera_.boost_multiplier;
            meta.roll_speed       = camera_.roll_speed;
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

        scene_dirty_ = false;
        editor_camera_dirty_ = false;
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
        if (!prefer_scene_camera_)
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
