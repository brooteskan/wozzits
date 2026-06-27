// src/engine/app/wozzits_app_v1.cpp

#include <engine/app/wozzits_app_v1.h>

#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_authoring_materialize.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/assets/scene/scene_json_export.h>
#include <engine/assets/scene_asset_module.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/type_extensions.h>

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
#include <unordered_set>
#include <utility>
#include <vector>

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

        // Exclude runtime-grafted instance children (#213): an instanced
        // scene_source re-imports from its reference at load, so its grafted
        // sub-tree must not be persisted as authored nodes (only the host node
        // with its scene_source reference is). Flattened nodes are authored (not
        // tracked here), so they persist normally.
        std::vector<wz::engine::assets::SceneNodeAsset> persisted_nodes;
        if (grafted_node_ids_.empty()) {
            persisted_nodes = scene_nodes_;
        }
        else {
            const std::unordered_set<std::string> grafted(
                grafted_node_ids_.begin(), grafted_node_ids_.end());
            persisted_nodes.reserve(scene_nodes_.size());
            for (const wz::engine::assets::SceneNodeAsset& n : scene_nodes_) {
                if (grafted.count(n.id) == 0) {
                    persisted_nodes.push_back(n);
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
