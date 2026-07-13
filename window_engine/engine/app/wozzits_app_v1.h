#pragma once

// engine/app/wozzits_app_v1.h
//
// WozzitsApp_v1 — the base RHI render-app. It owns the current resolved asset
// graph + scene and a RhiSceneRenderer, and exposes per-frame operations. It
// deliberately does NOT own the while-loop or any device-frame boundaries:
// callers drive it. That is what lets two apps COMPOSE it rather than inherit:
//   - the thin runtime main (src/app/wozzits_app_v1) loops { sim_tick;
//     begin_frame; clear; render_scene; end_frame; present } -> the
//     compile-once / exported app.
//   - the editor (wozzits-imgui) owns its imgui loop, HAS-A WozzitsApp_v1, and
//     calls bind_asset_graph(draft) on edit + render_scene() per frame.
//
// It lives in the window_engine LIBRARY (not the wozzits_app_v1 executable) so
// both the executable and the editor — which link window_engine — can compose
// it. The executable's main.cpp is the only thing that stays in the app target.
//
// The asset-graph lifecycle is wholesale replacement, mirroring the editor:
// bind_asset_graph compiles a draft (materialize keys -> swap the registered set
// via replace_registered_assets -> resolve), invalidates the renderer's realized
// caches (releasing the outgoing graph's GPU resources) and re-bridges the
// scene's renderables to the new keys. Binding a different graph replaces the
// previous one. No incremental diffing is assumed.

#include <engine/app_context.h>
#include <engine/app/scene_change.h>
#include <engine/app/scene_document.h>
#include <engine/app/view_controller.h>
#include <engine/rendering/rhi_scene_renderer.h>

#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/audio/audio_runtime.h>
#include <engine/audio/scene_audio.h>
#include <engine/behavior/behavior_plugin_adapter.h>
#include <engine/behavior/behavior_registry.h>
#include <engine/frame_storage.h>
#include <engine/motion/motion_filter.h>

#include <asset/dag.h>
#include <asset/draft.h>
#include <bench/flying_camera.h>
#include <file/filesystem.h>
#include <input/input.h>
#include <math/mat4.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <source_location>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wz::app
{
    // Status returned to a graph-author (the editor renders these): ok plus the
    // per-node/edge validation diagnostics that say why it did not compile.
    // Reuses the engine's existing draft validation surface (#171 / typed ports).
    struct AssetGraphCompileResult
    {
        bool ok = false;  // false if any Error-severity diagnostic
        std::vector<wz::asset::AssetGraphDraftValidationMessage> diagnostics;
    };

    struct WozzitsAppSceneLoadDesc
    {
        // Paths are resource-root-relative or absolute. Project manifest loading
        // belongs outside this runtime render app.
        wz::fs::Path asset_graph;
        wz::fs::Path scene;

        // Optional directory of compiled project behavior-module DLLs (the
        // manifest's behavior_module_folder). When set, load_scene loads every
        // DLL there and registers its modules so the scene's behavior bindings
        // run. Empty => only the built-in behavior modules are available.
        wz::fs::Path behavior_module_folder;
    };

    // One entry in the project's scenelet (prefab) catalog: the prefab NAME (the
    // filename stem, e.g. "enemy_tank") + the scene file PATH (resource-root-
    // relative). The editor lists these so an author can pick a prefab to spawn --
    // and, ahead, to open for editing.
    struct SceneletCatalogEntry
    {
        std::string name;
        std::string path;
    };

    class WozzitsApp_v1
    {
    public:
        // Borrows the engine context (logger/window/device/assets), created by
        // wz::engine::init before this is constructed.
        explicit WozzitsApp_v1(wz::engine::AppContext& ctx);

        WozzitsApp_v1(const WozzitsApp_v1&)            = delete;
        WozzitsApp_v1& operator=(const WozzitsApp_v1&) = delete;

        // Replace the current asset graph by compiling a DRAFT in place:
        // materialize keys, swap the registered set, resolve, then rebind the
        // renderer (invalidate its realized caches + release the outgoing
        // graph's GPU resources) and re-bridge the scene's renderables to the
        // new keys. Returns ok + diagnostics. Compiling writes the resolved keys
        // and validation messages INTO the draft (that is the status the editor
        // reads back); AssetGraphDraft is move-only, so it is taken by reference.
        AssetGraphCompileResult bind_asset_graph(wz::asset::AssetGraphDraft& draft);

        // Replace the current asset graph by adopting an already-resolved DAG by
        // move (future async/baked handoff). True on success.
        bool adopt_asset_graph(wz::asset::AssetGraph&& resolved);

        // Load a scene + asset graph, then compile the graph once and bind.
        bool load_scene(const WozzitsAppSceneLoadDesc& desc);

        // Live scene-node edit: overwrite the local transform of the node with
        // `id`, returning false if no node has that id. #221: the edit lands in
        // the live simulation polytree through the single apply_node_local_-
        // transform seam (NOT document_.nodes()) — the polytree is the source of truth
        // the next render_scene() draws from and save_scene derives from, so one
        // write covers render, save, and editor read-back with no GPU rebuild.
        // Persisting the edit to the scene file on disk is a separate path.
        bool set_node_transform(
            const wz::scene::AuthoredEntityId& id,
            const wz::engine::assets::AuthoredTransform& transform);

        // Live scene-node add: append a new child (no components, no label) under
        // `parent_id` (empty => top level) in the in-memory scene, minting a
        // counter id. Returns {ok, new_id, error}; rejects a missing parent. The
        // next render_scene() includes it; persistence is a separate path.
        wz::engine::assets::SceneAddChildResult add_child_node(
            const wz::scene::AuthoredEntityId& parent_id);

        // Live scene-node properties edit: set the node's label (name) and
        // visibility in the in-memory scene; false if no node has that id. The
        // renderer skips invisible nodes, so this hides/shows it live.
        // Persistence is a separate path.
        bool set_node_properties(
            const wz::scene::AuthoredEntityId& id,
            std::string name,
            bool visible);

        // Live reparent: set the node's parent (empty => top level) in the
        // in-memory scene; false on rejection (missing node, self, or cycle).
        bool reparent_node(
            const wz::scene::AuthoredEntityId& id,
            const wz::scene::AuthoredEntityId& new_parent_id);

        // Live delete: remove the node and its subtree from the in-memory scene.
        // Returns false if no node had that id.
        bool remove_node(const wz::scene::AuthoredEntityId& id);

        // Live reorder: move `id` to just before `before_id` in the flat scene
        // list (empty => move to the end), changing only its draw order (array
        // position); nesting/transforms are parent_id-based and untouched. Re-
        // applies the render_order sort so coarse layers stay dominant. Returns
        // false when nothing changed (missing id, absent target, or already in
        // place). The apply behind the editor's scene-tree reorder.
        bool reorder_node(
            const wz::scene::AuthoredEntityId& id,
            const wz::scene::AuthoredEntityId& before_id);

        // Live render_order (draw-order LAYER) edit: set the node's render_order
        // key and re-bake draw order, so the node moves to its layer. This is the
        // cross-cutting layer override (within a layer, order is the tree /
        // reorder). Returns false when nothing changed (missing id or same value).
        // The apply behind the editor's render-layer dropdown.
        bool set_node_render_order(
            const wz::scene::AuthoredEntityId& id,
            int render_order);

        // ─── Live behavior-binding authoring ─────────────────────────────────
        // Apply behind the host ABI's behavior verbs. Each mutates the matching
        // node's behavior binding(s) in document_.nodes(), marks the scene dirty, and
        // re-materializes the behavior runtime (rebuild_behavior_scene) so the
        // change takes effect on the next dispatch. All return false if no
        // node/binding matched (add_node_behavior returns {ok,id,error}).

        // Add a behavior binding (the given module, a minted stable binding id)
        // to the node; returns the minted id (or an error if the node is absent).
        wz::engine::assets::SceneAddBehaviorResult add_node_behavior(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& module);

        // Remove a behavior binding from the node by id.
        bool remove_node_behavior(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id);

        bool set_node_behavior_enabled(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id,
            bool enabled);

        bool set_node_behavior_fields(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id,
            const std::string& label,
            const std::string& module);

        bool set_node_behavior_events(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id,
            const std::vector<std::string>& events);

        bool set_node_behavior_config(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id,
            const wz::engine::assets::SceneBehaviorConfigValue& value);

        bool clear_node_behavior_config(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& binding_id,
            const std::string& key);

        // ─── Live optional-component authoring ───────────────────────────────
        // Apply behind the host ABI's component verbs. Add/remove one of the
        // five editor-managed optional node components by a kind token
        // ("camera", "renderable", "proximity", "collision", "motion") on the
        // in-memory scene (document_.nodes()), and mark the scene dirty on success.
        // The renderer consumes document_.nodes() fresh each frame, so the next
        // render reflects the change with no GPU rebuild; persistence is a
        // separate path. None of these five participate in the behavior runtime,
        // so neither rebuilds the behavior scene. Both return false (no-op) if
        // the node is missing or the kind is not one of the five (fail closed).

        // Add the optional component `kind` (default-constructed) to the node.
        bool add_node_component(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& kind);

        // Remove the optional component `kind` from the node.
        bool remove_node_component(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& kind);

        // Apply behind the host ABI's set_node_renderable_asset verb. Author the
        // PREFERRED asset-graph-backed Renderable component on the node: point it
        // at the authored asset-graph node `asset_graph_node_id`, or clear the
        // renderable when the id is 0. The resolved AssetKey is reset so it
        // re-resolves; the legacy embedded renderable slot is left untouched.
        // Marks the scene dirty on success; the renderer reads document_.nodes() each
        // frame so the next render reflects it, and the behavior runtime is not
        // rebuilt. Returns false (logged no-op) if the node is missing.
        bool set_node_renderable_asset(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);

        // Apply behind the host ABI's audio authoring verbs. set_node_audio_-
        // renderable points the node's AudioSource at the authored audio-renderable
        // asset-graph node (creating the component on a non-zero pick; clearing
        // the reference on 0); the resolved key re-resolves at bind.
        // set_node_audio_source_play sets the AudioSource's auto_play/enabled (no-op
        // if the node has no AudioSource). Both mark the scene dirty on success and
        // return false (logged no-op) if the node is missing.
        bool set_node_audio_renderable(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);

        bool set_node_audio_source_play(
            const wz::scene::AuthoredEntityId& node_id,
            bool auto_play,
            bool enabled);

        // Apply behind the host ABI's set_node_scene_source verb (issue #213).
        // Author the PREFERRED asset-graph-backed Scene-source component on the
        // node: point it at the authored "Scene from GLB" asset-graph node
        // `asset_graph_node_id`, or clear the scene source when the id is 0. The
        // resolved Scene AssetKey is reset so it re-resolves; the renderable slot
        // is left untouched. Marks the scene dirty on success and, if the graph
        // is bound, re-bridges + re-grafts so the referenced sub-scene's named
        // children appear under the host immediately (renderer reads document_.nodes()
        // each frame; the behavior runtime is rebuilt so the children are
        // addressable). Returns false (logged no-op) if the node is missing.
        bool set_node_scene_source(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);

        // Apply behind the host ABI's set_node_geometry_asset verb (issue #213
        // increment 2). Author the GEOMETRY half of the node's renderable
        // binding: point it at the authored geometry asset-graph node
        // `asset_graph_node_id`, or clear it (the node stops drawing) when the id
        // is 0. Re-assembles the binding (geometry + the effective, possibly
        // inherited, render program) and re-resolves so the next render reflects
        // it; marks the scene dirty. False (logged no-op) if the node is missing.
        bool set_node_geometry_asset(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);

        // Apply behind the host ABI's set_node_render_program verb (issue #213
        // increment 2). Author the RENDER-PROGRAM half of the binding (inherited
        // down the scene tree): point the node at the authored render-program
        // asset-graph node `asset_graph_node_id`, or clear it when the id is 0.
        // Re-assembles bindings for this node AND its descendants (a program
        // change cascades via inheritance) and re-resolves; marks the scene
        // dirty. False (logged no-op) if the node is missing.
        bool set_node_render_program(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);

        // Author one semantic resource binding of the node's custom-renderable
        // ingredients (issue #229): point layout row `semantic` at the
        // authored asset-graph source `asset_graph_node_id` (upserting the
        // row), or remove the row when the id is 0. A binding present makes
        // the assembled renderable the CUSTOM (0x70A) form, so this
        // re-assembles + re-resolves like the geometry/program seams; marks
        // the scene dirty. False (logged no-op) if the node is missing or the
        // semantic is empty.
        bool set_node_renderable_binding(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& semantic,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id);

        // Author one per-instance constant override of the node's
        // custom-renderable ingredients (issue #229): upsert `name` with
        // `value` (4 floats; a narrower declared field consumes a prefix), or
        // remove the override when `value` is null. Instance overrides merge
        // at PACK time over the synthesized recipe's defaults, so this
        // mutates the node ONLY — no re-assembly, no asset-graph recompile,
        // no re-key; the next rendered frame reflects it. Marks the scene
        // dirty. False (logged no-op) if the node is missing or the name is
        // empty.
        bool set_node_renderable_constant(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& name,
            const float* value);

        // The node's current per-instance override value for renderable
        // constant `name`, or nullopt if the node is missing or carries no
        // override for that name. Reads the authored SceneNodeAsset (which
        // survives a behavior-runtime rebuild), so it observes both a live
        // set_node_renderable_constant and the SET_RENDERABLE_PARAM behavior
        // verb (#232).
        [[nodiscard]] std::optional<std::array<float, 4>>
        node_renderable_constant(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& name) const;

        // Apply behind the host ABI's set_node_collision verb (issue #216/#217).
        // Author the node's Collision component by REFERENCE: point it at an
        // authored asset-graph collision node `asset_graph_node_id` (e.g.
        // collision_from_height_field) and set whether the resolved surface
        // constrains Motion actors (constrain_movement). Id 0 clears the
        // reference. Creates the Collision component if absent, re-bridges the
        // reference to the bound graph's collision key, and rebuilds the runtime
        // scene so the constraint surface takes effect; marks the scene dirty.
        // False (logged no-op) if the node is missing.
        bool set_node_collision_asset(
            const wz::scene::AuthoredEntityId& node_id,
            wz::asset::AssetGraphDraftNodeId asset_graph_node_id,
            bool constrain_movement);

        // Apply behind the host ABI's set_node_motion_terrain verb (issue
        // #216/#217). Set the terrain-stick fields of the node's Motion
        // component (creating it if absent): whether the actor is constrained to
        // the terrain surface, its ride height + footprint radius, and whether/
        // how strongly it aligns to the surface normal. #221: when the node
        // already has a live Motion record, the fields are patched in place on
        // that record (preserving the runtime-only terrain_alignment_rate a
        // behavior set) so a per-frame field tweak does NOT rebuild the runtime
        // (which would reset behavior/sim state); only ADDING the Motion component
        // (no live record yet) falls back to a full rebuild_behavior_scene. Marks
        // the scene dirty. False (logged no-op) if the node is missing.
        bool set_node_motion_terrain_fields(
            const wz::scene::AuthoredEntityId& node_id,
            bool terrain_constrained,
            float ride_height,
            float footprint_radius,
            bool align_to_surface,
            float alignment_strength);

        // Live edit: set the node's whole Motion Filter component. Adds the
        // component if absent. Patches the live record in place when it exists
        // (no rebuild); adding it rebuilds so apply_motion_filters sees it.
        bool set_node_motion_filter(
            const wz::scene::AuthoredEntityId& node_id,
            const wz::engine::assets::SceneMotionFilterAsset& filter);

        // Flatten the node's referenced Scene asset into the live scene (#213):
        // resolve the node's scene_source, expand its GLB-named nodes as real,
        // persistent children of the node in document_.nodes() (id "<host>/<glbname>",
        // sub-scene parenting preserved, transforms composed under the host), and
        // then DROP the node's scene_source reference (the expansion is now the
        // authored content — fully editable, no live link). Marks the scene dirty
        // and rebuilds the behavior runtime. Returns false (logged) if the node
        // is missing, carries no scene_source, or the referenced scene can't be
        // resolved. The editor calls this for the "flatten" consume mode.
        bool flatten_scene_source(const wz::scene::AuthoredEntityId& node_id);

        // Apply behind the host ABI's set_node_glb_scene_source verb (issue #213,
        // the asset-graph-INDEPENDENT route). Author the self-contained GLB
        // scene-source DESCRIPTOR on the node — a resource-relative GLB path +
        // scene_index + consume_mode (Phase 3a always supplies an empty
        // base_style/style_overrides => the single built-in default style). An
        // empty descriptor.path CLEARS the descriptor (detach_scene_source); any
        // asset-graph-node scene-source route on the node is dropped so the node
        // stays single-route. Marks the scene dirty on success and, if an asset
        // library is present, re-materializes immediately so the change shows on
        // the next frame: resolve_glb_scene_sources (re-register the GLB + Scene
        // asset and write the resolved key), commit + resolve_all (compile the
        // freshly registered assets), graft_scene_sources (graft/drop the host's
        // children), then rebuild the behavior runtime (the grafted children
        // change the entity set) — the same sequence load_scene runs for the
        // descriptor route. Returns false (logged no-op) if the node is missing.
        bool set_node_glb_scene_source(
            const wz::scene::AuthoredEntityId& node_id,
            const wz::engine::assets::SceneGLBSceneSource& descriptor);

        // ─── Per-component GLB render-style authoring (issue #213 Phase 3b-2) ──
        // Mutate the styling baked into a node's glb_scene_source DESCRIPTOR, then
        // re-materialize so the re-keyed Scene rebuilds with the new look (the
        // descriptor's styles fold into create_scene_from_glb's content key, so a
        // style change re-keys + rebuilds the per-mesh renderables). Each writes
        // the style into the persisted descriptor so a headless load renders the
        // same result with no editor — styling is DATA, never editor-only state.
        // All return false (logged no-op) if the node has no glb_scene_source.

        // Set the descriptor's base style (applied to every imported mesh unless a
        // per-mesh override wins).
        bool set_node_glb_base_style(
            const wz::scene::AuthoredEntityId& node_id,
            const wz::engine::assets::MeshRenderStyleData& style);

        // Set (replace-or-insert) the per-mesh-index override for `mesh_index`.
        bool set_node_glb_mesh_style(
            const wz::scene::AuthoredEntityId& node_id,
            uint32_t mesh_index,
            const wz::engine::assets::MeshRenderStyleData& style);

        // Clear the per-mesh-index override for `mesh_index` (the mesh falls back
        // to the base style). A no-op-success if no such override exists.
        bool clear_node_glb_mesh_style(
            const wz::scene::AuthoredEntityId& node_id,
            uint32_t mesh_index);

        // True if node `node_id` currently carries the optional component `kind`.
        // False if the node is missing or the kind is unknown. Lets diagnostics
        // and the component-authoring test observe presence without a snapshot.
        [[nodiscard]] bool node_has_component(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& kind) const;

        // Number of live scene nodes whose direct parent is `parent_id`. Lets
        // the behavior-driven add_child test observe a spawned child landing in
        // document_.nodes() without a snapshot (mirrors node_has_component). Counts
        // direct children only.
        [[nodiscard]] std::size_t child_node_count(
            const wz::scene::AuthoredEntityId& parent_id) const;

        // The node's authored asset-graph renderable node id, or nullopt if the
        // node is missing or has no preferred renderable bound. Lets the
        // renderable-authoring test observe set_node_renderable_asset without a
        // snapshot (mirrors node_has_component).
        [[nodiscard]] std::optional<wz::asset::AssetGraphDraftNodeId>
        node_renderable_asset_node_id(
            const wz::scene::AuthoredEntityId& node_id) const;

        // The node's authored scene-source graph-node id, or nullopt if the node
        // is missing or has no scene source. Lets the scene-source test observe
        // set_node_scene_source without a snapshot (mirrors the renderable one).
        [[nodiscard]] std::optional<wz::asset::AssetGraphDraftNodeId>
        node_scene_source_node_id(
            const wz::scene::AuthoredEntityId& node_id) const;

        // True if node `node_id` currently carries a glb_scene_source DESCRIPTOR
        // (issue #213, the asset-graph-independent route). Lets the GLB
        // scene-source test observe the descriptor's lifecycle without a snapshot
        // — notably that a Flatten-mode load DROPS it (the expansion is now the
        // content) while an Instance-mode load KEEPS it (the live link persists).
        // False if the node is missing.
        [[nodiscard]] bool node_has_glb_scene_source(
            const wz::scene::AuthoredEntityId& node_id) const;

        // The node's current GLB scene-source DESCRIPTOR (issue #213), or nullptr
        // if the node is missing or has none. Lets the Phase 3b-2 style test
        // observe the authored base_style / style_overrides without a snapshot
        // round-trip (mirrors node_has_glb_scene_source). The pointer is into
        // document_.nodes() — valid only until the next scene mutation.
        [[nodiscard]] const wz::engine::assets::SceneGLBSceneSource*
        node_glb_scene_source(
            const wz::scene::AuthoredEntityId& node_id) const;

        // The node's current Collision component (issue #216/#217), or nullptr if
        // the node is missing or has none. Lets the collision-authoring test
        // observe set_node_collision_asset's effect (the bridged collision_asset
        // key + constrain_movement) without a snapshot. The pointer is into
        // document_.nodes() — valid only until the next scene mutation.
        [[nodiscard]] const wz::engine::assets::SceneCollisionAsset*
        node_collision(const wz::scene::AuthoredEntityId& node_id) const;

        // The node's current Motion component (issue #216/#217), or nullptr if
        // the node is missing or has none. Lets the motion-terrain test observe
        // set_node_motion_terrain_fields without a snapshot. Same pointer-lifetime
        // caveat as node_collision.
        [[nodiscard]] const wz::engine::assets::SceneMotionAsset*
        node_motion(const wz::scene::AuthoredEntityId& node_id) const;

        // A COPY of just the runtime-grafted scene nodes (issue #213): the
        // entries of document_.nodes() whose id is in document_.grafted_ids() — the
        // instance-mode sub-trees appended by graft_scene_sources. These live
        // only in the live runtime (an instanced scene_source re-imports from its
        // reference; the children are never persisted as authored nodes), so the
        // editor's JSON-sourced scene tree never has them. The host ABI surfaces
        // this (as a scene snapshot) so the editor can merge the grafts under
        // their hosts. Each entry keeps its parent_id (the host's id, or a deeper
        // graft's id) so the merger can place it. Engine-thread only (reads
        // document_.nodes()); the editor reaches it through the blocking
        // request_grafted_scene_nodes handshake. Empty when nothing is grafted.
        [[nodiscard]] std::vector<wz::engine::assets::SceneNodeAsset>
        grafted_scene_nodes() const;

        // The AUTHORED scene nodes (document_.nodes() minus the runtime-only grafted +
        // "spawn:" nodes, the same set save_scene persists) -- a snapshot of the
        // working scene for the editor to reload its tree after open_scene swaps the
        // scene. Engine-thread only; the editor reaches it through the blocking
        // request_scene_nodes handshake.
        [[nodiscard]] std::vector<wz::engine::assets::SceneNodeAsset>
        authored_scene_nodes() const;

        // Persist the current scene back to its source file: the nodes are
        // re-emitted, all other scene data preserved. No-op (returns true) when
        // no live edit happened since load/last save; false on write failure.
        bool save_scene();

        // Frame profiling is opt-in (default OFF). OFF records nothing, so the
        // exit-time flush is a no-op and no frame_profile_<tag>.csv is written
        // (fixes the file spam + slow shutdown). Toggling ON starts a fresh
        // capture; toggling OFF flushes what was captured to its own file; app
        // close flushes too (via save_scene). Driven by the editor menu through
        // the runtime control seam.
        void set_frame_profiling_enabled(bool enabled);
        [[nodiscard]] bool frame_profiling_enabled() const noexcept
        {
            return frame_profiling_enabled_;
        }

        // Scene-simulation start/stop gates (#258) — pause the running scene in
        // place without unloading it, for a future editor play/pause button. The
        // two axes are independent:
        //   - simulation = motion integration + terrain constraints + motion
        //     filters (the physics advance).
        //   - behaviors  = behavior dispatch + cognition + SELF_ACTIVATED edges +
        //     command apply + spawn/authoring drains (the AI/scripts).
        // Both default ON (normal play + edit). Behaviors OFF freezes AI/scripts;
        // simulation OFF freezes motion; BOTH off leaves the static authored
        // composition (free-fly navigation + live edit still work). The live-mask
        // is refreshed every tick regardless (collision + a resumed dispatch read
        // it) and prev-active is rolled every tick, so resuming behaviors does NOT
        // replay a burst of activation edges accrued while paused.
        void set_simulation_enabled(bool enabled) { simulation_enabled_ = enabled; }
        [[nodiscard]] bool simulation_enabled() const noexcept
        {
            return simulation_enabled_;
        }
        void set_behaviors_enabled(bool enabled) { behaviors_enabled_ = enabled; }
        [[nodiscard]] bool behaviors_enabled() const noexcept
        {
            return behaviors_enabled_;
        }

        // Swap the WORKING SCENE to a different scene file (the prefab-editor's
        // open: point the editor at a scenelet, edit it with the normal tools, then
        // save_scene() writes back to it; open_scene the main scene again to switch
        // back). Reuses the asset graph + module folder from the last load_scene, so
        // the scenelet renders against the SAME project asset graph. Fails if no
        // scene was loaded yet (no asset-graph path to reuse) or the load fails.
        bool open_scene(const wz::fs::Path& scene_path);

        // Carve the subtree rooted at `root_node_id` out of the live scene and
        // write it to `out_path` as a fresh, self-contained scene.json (the
        // prefab-system milestone): the root node + its transitive descendants
        // become the new scene's nodes, with the root re-rooted to the origin so
        // a future spawn places the prefab purely by the spawn transform (see
        // extract_scene_subtree). Runtime-grafted scene_source children (#213,
        // document_.grafted_ids()) are excluded, mirroring save_scene — a prefab keeps
        // a scene_source host's reference, not its grafted subtree. `out_path` is
        // resolved like save_scene's (absolute as-is, else joined onto the
        // resource root). Emits a FRESH document (a prefab is self-contained; no
        // merge into an existing file). Does NOT touch the live scene or
        // document_.dirty(). Returns false (and logs) if `root_node_id` doesn't
        // resolve or the write fails.
        bool export_subtree_as_scene(
            std::string_view root_node_id,
            const wz::fs::Path& out_path);

        // Register a prefab ("scenelet" Scene asset) so a behavior can spawn it at
        // runtime by name (the second prefab-system milestone, runtime spawning).
        // The prefab is keyed by the FNV-1a/32 hash of `prefab_name` — the same
        // hash wz_prefab_hash / wz_write_spawn_prefab use — so a SPAWN_PREFAB
        // command naming it resolves to these nodes. `nodes` is a self-contained
        // scenelet (a root + its descendants, as produced by extract_scene_subtree
        // / export_subtree_as_scene): on spawn the host clones it with conflict-
        // free ids and grafts it under the computed spawn transform. A second
        // registration with the same name replaces the prior nodes. The spawn-apply
        // resolves the prefab once and caches it.
        void register_prefab(
            const std::string& prefab_name,
            std::vector<wz::engine::assets::SceneNodeAsset> nodes);

        // Number of live scene nodes spawned from prefabs (the spawn graft).
        // Lets the spawn end-to-end test observe a prefab landing in document_.nodes()
        // / the behavior runtime without a snapshot. Counts every node whose id
        // carries the "spawn:" prefix this milestone mints.
        [[nodiscard]] std::size_t spawned_prefab_node_count() const;

        // The authored id of the currently selected scene-camera anchor, or an
        // empty string when no scene camera is selected (free-fly active). Lets the
        // #219 guard confirm a prefab spawn (which rebuilds the behavior runtime
        // mid-tick) does NOT flip the active camera off its anchor. Read-only
        // diagnostic, mirroring the node_* accessors.
        [[nodiscard]] const wz::scene::AuthoredEntityId&
        active_scene_camera_id() const;

        // Reload the project's behavior-module DLLs from `module_folder` into a
        // clean registry and re-materialize the behavior runtime, without
        // restarting the engine. Used by the editor after it recompiles the
        // project's behavior sources: clears registry_ + plugins_, re-registers
        // the built-ins (they are otherwise only registered in the ctor), reloads
        // every DLL in the folder (load_behavior_modules), then rebuilds the
        // behavior scene. Per-module load results are logged. No-op-safe for an
        // empty/missing folder (built-ins are still restored).
        void reload_behavior_modules(const wz::fs::Path& module_folder);

        // Names of every currently registered behavior module (built-ins + the
        // project DLLs loaded from behavior_module_folder) — the set a scene-node
        // behavior binding may reference. Lets the editor offer "add a behavior"
        // from the imported modules. Order follows registration.
        [[nodiscard]] std::vector<std::string> behavior_module_names() const;

        // The project's scenelets (prefabs), gathered when the scene loaded: each is
        // a spawnable/editable prefab (name + resource-relative path). The editor
        // lists these for its scenelet menu. Empty until load_scene has scanned the
        // scenelets folder.
        [[nodiscard]] std::vector<SceneletCatalogEntry> scenelet_catalog() const;

        // Per-frame operations. The caller owns the loop and the device-frame
        // boundaries (begin_frame/clear/end_frame/present). simulation_tick takes
        // the frame's input + dt so the app drives its own free-fly camera (the
        // game-app-parity runtime camera); pass a default-constructed InputState
        // to tick with no input. drive_camera gates the fly-cam's consumption of
        // the input (the host arms it, e.g. on viewport focus); behaviors always
        // receive the input so a controller can drive the scene independently.
        void simulation_tick(
            const wz::input::InputState& input,
            float dt,
            bool drive_camera = true);
        bool render_scene();      // record scene draws (between begin/end frame)

        // The world transform every render-side consumer draws with, one matrix
        // per document_.nodes() entry (index-aligned). #221's single source of truth:
        //   - with a live behavior scene, each node's world matrix is read from
        //     the simulation polytree (authored id -> runtime entity ->
        //     node_data().world), so behavior/motion/terrain-constraint movement
        //     is visible without ever mutating document_.nodes(). A node absent from
        //     the runtime map falls back to its authored composition.
        //   - with no behavior scene (nothing needs simulation), it is exactly
        //     compute_scene_node_world_transforms(document_.nodes()), as before.
        // render_scene(), the audio spatialization, and prefab spawn anchoring
        // all read this so they never diverge.
        [[nodiscard]] std::vector<wz::math::Mat4> scene_world_transforms() const;

        // The render-side WORLD matrix of node `id` (its entry in
        // scene_world_transforms()), or std::nullopt if no node has that id. #221
        // diagnostics/tests read this to observe the drawn pose — sim-current with
        // a live behavior scene — without knowing the node's index.
        [[nodiscard]] std::optional<wz::math::Mat4> node_world_transform(
            const wz::scene::AuthoredEntityId& id) const;

        // Camera-source policy for this host. Standalone/play (no editor control)
        // passes true so a scene-authored camera, once selected on load, drives
        // the view; the editor edit viewport passes false so the free-fly camera
        // stays active and you can navigate even when the scene authors a camera.
        // Only gates whether selection FLIPS the active source to the scene
        // camera -- the selection anchor is recorded either way, so a future
        // "look through scene camera" editor toggle is a cheap source switch.
        void set_prefer_scene_camera(bool prefer);

        // Number of GPU resources currently resident in the renderer's resource
        // registry. Stays flat across a graph swap (the outgoing graph's
        // resources are released as the new graph's are realized) — diagnostics
        // and the rebind regression test rely on this.
        [[nodiscard]] std::size_t resident_gpu_resource_count() const;

        // Number of scene nodes that currently carry a resolved renderable key.
        // Drops to 0 if a graph swap removes the authored renderables (the keys
        // are re-bridged, not left stale) — the rebind test asserts on this.
        [[nodiscard]] std::size_t resolved_renderable_node_count() const;

        // rhi render-program / shader-module registry occupancy (on the shared
        // EngineGpuContext). The graph-swap path clears them before resolve, where
        // the asset compiler re-registers the produced program/shaders, so the
        // fixed-capacity registries stay bounded across editor rebinds; an empty
        // graph drops both to 0.
        [[nodiscard]] std::size_t registered_program_count() const;
        [[nodiscard]] std::size_t registered_shader_count() const;

        // SRV descriptor tables cached by the renderer's command recorder; resets
        // to 0 on a graph swap so descriptor-heap ranges don't leak across binds.
        [[nodiscard]] std::size_t cached_descriptor_table_count() const;

        // Count of render programs the renderer bridged at render time (the
        // fallback path) rather than binding one the asset compiler produced. 0
        // after the first render of the migrated custom program proves the
        // compiler-produced path is taken (#192). Cumulative since construction.
        [[nodiscard]] std::size_t render_time_program_bridge_count() const;

        // Number of behavior bindings instantiated for the loaded scene (sum of
        // every node's `behavior`/`behaviors`). 0 when no scene is loaded or the
        // scene has no behaviors. Diagnostics + the behavior dispatch test read
        // this to confirm the scene's behaviors were materialized.
        [[nodiscard]] std::size_t active_behavior_binding_count() const;

        // Reported local translation of the live scene node with `id`, or
        // std::nullopt if no node has that id. #221: with a live behavior scene
        // this is DERIVED from the simulation polytree (a behavior/motion that
        // moves the node shows here after a simulation_tick), so the editor
        // read-back + the behavior dispatch test observe the sim-current pose;
        // with no sim it is the node's stored authored translation.
        [[nodiscard]] std::optional<wz::math::Vec3> node_local_translation(
            const wz::scene::AuthoredEntityId& id) const;

        // Reported local scale of the live scene node with `id`. Like
        // node_local_translation, it is derived from the polytree when the sim is
        // live, so a test can confirm a scale edit on a sim-driven (e.g. terrain
        // constrained) node survives a simulation_tick (#218 follow-up).
        [[nodiscard]] std::optional<wz::math::Vec3> node_local_scale(
            const wz::scene::AuthoredEntityId& id) const;

        // The STORED authored local translation of scene node `id` — read
        // straight from document_.nodes(), never derived from the sim polytree. #221:
        // document_.nodes() transforms are no longer mutated per frame, so this lets a
        // test assert the authored data stays put while the render/sim pose
        // (node_local_translation / scene_world_transforms) moves. std::nullopt if
        // no node has that id.
        [[nodiscard]] std::optional<wz::math::Vec3> stored_node_local_translation(
            const wz::scene::AuthoredEntityId& id) const;

        // The resolved renderable key currently assembled for scene node `id`
        // (issue #229 test seam): what render_scene will draw for the node.
        // std::nullopt if no node has that id OR the node has no resolved
        // renderable. Lets a test fetch the synthesized custom renderable's
        // recipe through the asset library without re-deriving the key.
        [[nodiscard]] std::optional<wz::asset::AssetKey> node_renderable_asset(
            const wz::scene::AuthoredEntityId& id) const;

    private:
        // Resolve the selected scene camera's live world transform (from the
        // behavior scene's polytree, when the Scene source is active) and hand it
        // to view_.update_active_view() so it materializes the single active view
        // render_scene draws with. Called once per simulation_tick after both
        // sources are current (the free-fly camera was updated from input and
        // behaviors moved the scene camera node). This is the app's REACTION half
        // of the view seam: the scene lookup lives here, the camera math in
        // ViewController.
        void materialize_active_view();

        // #221 single edit seam for transforms: write `transform` as the LOCAL
        // pose of node `id` into the live simulation polytree (the same const_cast
        // idiom the constraint pipeline / behavior_command_apply use), then set
        // document_.dirty(). The polytree is now the single source of truth for the
        // drawn + saved pose (scene_world_transforms / derived_authored_transform
        // read it), so this ONE write covers render, save, and editor read-back —
        // no document_.nodes() transform mutation. DEGENERATE fallback: if there is no
        // live polytree at all (a failed instantiate left behavior_scene_ null, so
        // the editor must still recover), the seam writes document_.nodes().local
        // directly so the authored transform is not lost. set_node_transform and
        // the future authoring pokes route through here so there is one place a
        // transform edit lands. Assumes the node exists (the caller resolved it).
        void apply_node_local_transform(
            const wz::scene::AuthoredEntityId& id,
            const wz::engine::assets::AuthoredTransform& transform);

        // The authored LOCAL transform of node `id` as it should be reported /
        // saved. #221: with a live behavior scene the transform is derived from
        // the simulation polytree's LOCAL matrix (decomposed to TRS), so the
        // editor read-back + save capture the sim-current pose without a per-frame
        // write-back into document_.nodes(). With no live sim (or a node absent from
        // the runtime map, or a matrix decompose_trs rejects) it is the node's own
        // stored authored transform. Returns std::nullopt only if no node has `id`.
        [[nodiscard]] std::optional<wz::engine::assets::AuthoredTransform>
        derived_authored_transform(const wz::scene::AuthoredEntityId& id) const;

        // Load every behavior-module DLL in `module_folder` and register its
        // modules into registry_. No-op for an empty/missing folder. Reusable
        // across a scene/project swap (the plugin host reloads existing modules).
        void load_behavior_modules(const wz::fs::Path& module_folder);

        // Auto-register every "<resource_root>/scenelets/*.scene.json" as a prefab
        // (the runtime-spawning glue): each file is parsed into SceneAssetData and
        // its nodes registered via register_prefab under the filename stem (so
        // scenelets/tank.scene.json -> prefab "tank"). A behavior's SPAWN_PREFAB
        // command naming that stem then grafts it. Mirrors load_behavior_modules:
        // resolves the folder through the asset file system and is a silent no-op
        // when the scenelets folder is missing/empty. Returns the count registered.
        std::size_t register_scenelet_prefabs();

        // Resolve every node's glb_scene_source DESCRIPTOR into a Scene asset
        // (issue #213, the descriptor route). For each scene node carrying a
        // glb_scene_source, register the GLB file + call
        // SceneAssetModule::create_scene_from_glb with the descriptor's
        // scene_index + base_style + style_overrides, and write the returned
        // Scene key into the node's scene_source. Registers the produced assets
        // but does NOT compile them: the caller must commit() + resolve_all()
        // afterwards (so they resolve in the same pass) and then graft. Re-run on
        // every load + rebind so the GLB scenes survive the wholesale
        // replace_registered_assets (same content => same key => cache hit).
        // Returns the number of nodes whose scene_source was set. No-op (0) with
        // no asset library or no glb_scene_source nodes.
        std::size_t resolve_glb_scene_sources();

        // Assemble renderables from geometry+program BINDINGS (issue #213
        // increment 1b). For each scene node carrying geometry_asset_node_id:
        // resolve the geometry node id to its committed key + asset type, resolve
        // the EFFECTIVE render program (the node's own render_program_node_id,
        // else the nearest ancestor up parent_id that has one -- inheritance down
        // the scene tree), create the matching RHI renderable (kAssetTypeMesh ->
        // create_rhi_pull_mesh; kAssetTypeGpuSparseMesh ->
        // create_gpu_sparse_mesh_renderable), and set the node's renderable_asset
        // so the existing renderer draws it. Mirrors bridge_scene_renderable_keys
        // for resolution + resolve_glb_scene_sources for create-at-load: it
        // REGISTERS the renderables but does not compile them -- the caller must
        // commit() + resolve_all() afterwards. Returns the count assembled; no-op
        // (0) with no asset library or no geometry bindings.
        // only_node (optional): assemble just that one node's renderable and skip
        // the rest -- the incremental path for a single-node change (a custom-form
        // flip). nullptr = the whole scene (#253).
        // only_nodes (optional): the SUBTREE form of the same filter -- assemble only
        // the nodes in the set (a spawned subtree + its grafted children) and skip
        // the rest (#252). Both nullptr = the whole scene; both set is redundant but
        // safe (a node passes only if it satisfies every provided filter).
        std::size_t assemble_render_bindings(
            const wz::asset::AssetGraphDraft& draft,
            const std::string* only_node = nullptr,
            const std::unordered_set<std::string>* only_nodes = nullptr);

        // Dispatch the REACTION for a document edit (#258 avenue 2). Each edit verb
        // mutates the authored document and produces a SceneChange; this maps the
        // change kind to the reaction that keeps the runtime/renderer consistent
        // (Structural -> rebuild the behavior runtime when live; None -> nothing).
        // The mutation/reaction split is what lets the document logic move to the
        // engine while the reactions -- which touch behavior_scene_ / renderer_ /
        // the bound graph -- stay here. Kinds are handled as verbs are converted.
        void apply_scene_change(const SceneChange& change);

        // Re-assemble renderable bindings after one was edited live (issue #213
        // increment 2): re-bridge the pre-built renderables, re-run
        // assemble_render_bindings against the bound graph, then compile the
        // freshly created renderables (commit + resolve_all). The renderer reads
        // document_.nodes() each frame, so the next render reflects it. No-op without
        // an asset library.
        // caller defaults at the call site (source_location) so the per-frame
        // perf log names WHICH edit forced a re-materialize -- pins the spurious
        // spawn-time burst (4x in one frame, nothing structural changed; #252).
        void rematerialize_render_bindings(
            std::source_location caller = std::source_location::current());
        // Incremental single-node variant (#253): re-assemble ONLY node_id's
        // renderable (its recipe changed, e.g. a custom-form flip) + commit +
        // resolve. Skips the O(scene) full re-bridge + re-assemble; the renderer
        // reads document_.nodes() each frame, so the next render reflects it. No-op
        // without an asset library.
        void rematerialize_node_render_binding(const std::string& node_id);
        // Write the accumulated per-frame profile to
        // <resource_root>/frame_profile.csv through the data_table -> csv_export
        // asset chain (issue #252). No-op when nothing was recorded.
        void flush_frame_profile_csv();

        // Re-materialize the GLB scene-source descriptors after one was edited
        // (issue #213): re-resolve every descriptor into a Scene asset, compile
        // the freshly registered assets (commit + resolve_all), then re-graft the
        // hosts' children and rebuild the behavior runtime. The shared sequence
        // set_node_glb_scene_source and the style mutators run after mutating a
        // descriptor; factored out so the style edits reuse the exact 3a path.
        // No-op without an asset library (resolve/graft are guarded).
        void rematerialize_glb_scene_sources();

        // Graft every scene_source reference's sub-scene into document_.nodes() as
        // children of its host (issue #213, instance mode). For each host node
        // carrying a resolved scene_source key, resolve the referenced Scene
        // asset and append its expanded GLB-named children (via
        // expand_scene_source_children) so they render (document_.nodes() is the
        // renderer's source of truth) and are behavior-addressable. Idempotent
        // within a load: previously grafted children (id "<host>/...") are
        // removed first, so a re-bind/re-resolve re-grafts cleanly. Returns the
        // number of children grafted. Caller rebuilds the behavior scene after.
        std::size_t graft_scene_sources();

        // Incremental subset graft (#252): expand ONLY the given hosts' scene_source
        // sub-scenes, appending their children to document_.nodes() + document_.grafted_ids()
        // WITHOUT the drop-all the full graft does. Returns the newly grafted child
        // ids (so the caller can assemble just that subtree). Used by spawn_prefab so
        // a spawn touches only its own subtree; the parameterless graft_scene_sources()
        // stays the full drop-all re-graft for the bind/edit paths. A host id that is
        // not present or carries no resolved scene_source is skipped.
        std::vector<std::string> graft_scene_sources_for_hosts(
            const std::vector<std::string>& host_ids);

        // Expand one host's scene_source sub-scene: append its expanded children to
        // document_.nodes() + document_.grafted_ids(), re-apply the host's sticky per-child
        // overrides (#213), and append the new child ids to out_new_children when
        // non-null. Shared by graft_scene_sources() (full) and
        // graft_scene_sources_for_hosts() (subset). `host` MUST be a copy stable
        // across the document_.nodes() appends (push_back may reallocate). Returns the
        // number of children grafted.
        std::size_t graft_host_scene_source(
            const wz::engine::assets::SceneNodeAsset& host,
            const wz::asset::AssetKey& scene_source,
            std::vector<std::string>* out_new_children);

        // Mirror a grafted scene-source child's authored components onto its host
        // as a sticky override (issue #213), so an edit to a runtime-only grafted
        // child (which save_scene excludes) survives reload. No-op when child_id
        // is not a grafted node. Keyed by the child's sub-scene id (the "<host>/"
        // suffix); upserts the override from the child's current authored state,
        // erasing it when the child carries no overridable component. Re-applied
        // to the expanded child by graft_scene_sources on the next (re)graft.
        void capture_grafted_child_override(
            const wz::scene::AuthoredEntityId& child_id);

        // Materialize the live authored scene (document_.nodes()) into a runtime
        // SceneInstance and (re)initialize its behaviors against the registry.
        // Called from load_scene and after any structural scene edit so the
        // behavior runtime tracks the authored scene. Clears behavior_scene_
        // when there are no behavior bindings (nothing to run).
        //
        // append_only (#257 B1): pass true ONLY when the rebuild was triggered by a
        // pure APPEND to document_.nodes() (a prefab spawn) -- survivor runtime ids stay
        // stable then (handle == node index), so only the newly appended bindings need
        // their on_init run. Leave false for load / delete / reparent / flatten, which
        // reorder document_.nodes() and renumber, so every binding must re-init.
        void rebuild_behavior_scene(bool append_only = false);

        // Apply a SPAWN_PREFAB request: resolve `prefab_name_hash` to a registered
        // prefab, compute the spawn transform T = the spawner node's world
        // transform × the offset (offset applied in the spawner's frame), clone the
        // prefab nodes with conflict-free ids (instantiate_prefab_nodes), append
        // them to document_.nodes(), then rebuild the behavior runtime (now state-
        // preserving, so pre-existing bindings are untouched) and re-assemble
        // render bindings. The spawner is addressed by its STABLE authored id (not
        // a runtime entity), so the request stays valid even as a prior spawn in
        // the same frame-boundary drain rebuilds + renumbers the runtime. No-op
        // (logged) if the spawner id isn't in document_.nodes() or the prefab hash is
        // unknown. Drained at the frame boundary (like the deferred-authoring
        // edits); play-mode oriented (the spawned subtree's behaviors run next tick).
        // Returns the spawned root's STABLE authored id ("spawn:N:<root>") so the
        // identity-spawn path (#252 pooling) can address the instance later.
        // spawn_parked (#252): graft the subtree with active=0 + visible=0 (a
        // prewarmed pool instance is inert + hidden until acquired).
        struct SpawnPrefabResult
        {
            bool ok = false;
            wz::scene::AuthoredEntityId root_authored_id;
        };
        SpawnPrefabResult spawn_prefab(
            const wz::scene::AuthoredEntityId& spawner_id,
            uint32_t prefab_name_hash,
            float offset_x,
            float offset_y,
            float offset_z,
            bool spawn_parked = false);

        // One-shot WZ_EVENT_SCENE_LOADED dispatch after the scene is
        // materialized: lets a scene-setup behavior pick the active camera via
        // SET_ACTIVE_CAMERA. Called once from load_scene, before the frame loop.
        void select_scene_loaded_active_camera();

        // One-shot scene-audio start: in play mode (prefer_scene_camera_), open
        // the output device if needed and auto-play the materialized scene's
        // AudioSources through the mixer. No-op in the editor (silent until an
        // audition path lands) or when no device is available. Called once from
        // load_scene after the behavior scene is materialized.
        void start_scene_audio();

        // Auto-play the AudioSources of a freshly SPAWNED prefab subtree. The
        // scene-load start_scene_audio() pass runs ONCE, before any spawn exists,
        // so a spawned node's auto_play source (e.g. a spawned tank's engine grain
        // cloud) would otherwise never start. Called at the end of spawn_prefab; it
        // re-runs the auto-play pass but skips every client id already started
        // (tracked in auto_played_clients_), so only the new subtree's sources fire.
        void start_spawned_audio();

        // Apply a SET_ACTIVE_CAMERA command: resolve the runtime entity to its
        // authored node, look up its SceneCameraAsset params in the scene document
        // (the app's half), then hand id + entity + params to
        // view_.select_scene_camera() which records the anchor and flips the source
        // to Scene when prefer_scene_camera is set. The view itself is built later
        // by materialize_active_view().
        void apply_scene_active_camera(wz::scene::RuntimeEntityId runtime_entity);

        // Re-seat view_'s live camera entity handle at the anchored authored id's
        // runtime entity in the current behavior scene. Called once after each
        // rebuild (NOT per frame), so the Scene camera source survives a
        // behavior-scene rebuild (e.g. a behavior spawning a child). Passes INVALID
        // when no camera is selected or the node is gone.
        void refresh_active_camera_entity();

        // One behavior tick: propagate transforms, dispatch frame/input events,
        // apply the produced command buffer + integrate motion, then write the
        // changed node transforms back into document_.nodes() so the next
        // render_scene() draws them. No-op when no behavior scene is live.
        void dispatch_scene_behaviors(
            const wz::input::InputState& input, float dt);

        // A SPAWN_PREFAB request resolved to the spawner's STABLE authored id,
        // collected during the command pass and drained at the frame boundary.
        // A spawn grafts nodes + rebuilds/renumbers the behavior runtime, so it
        // must NOT run mid command-pass (that would strand the runtime ids the
        // remaining commands address); apply_all_behavior_commands COLLECTS these
        // and dispatch_scene_behaviors drains them once the pass is done.
        struct DeferredSpawnRequest
        {
            wz::scene::AuthoredEntityId spawner_id;
            uint32_t                    name_hash = 0u;
            float                       offset[3]{ 0.0f, 0.0f, 0.0f };
        };

        // The single converged drain for a produced behavior command buffer
        // (#256 seam A). Applies the transform/velocity kinds (through
        // apply_behavior_commands) AND every host-handled IMMEDIATE kind -- audio
        // (play / stop / gain / grain), SET_RENDERABLE_PARAM, SET_NODE_VISIBLE,
        // SET_NODE_ACTIVE, SET_ACTIVE_CAMERA -- then COLLECTS the deferred
        // SPAWN_PREFAB requests into out_spawn_requests for the caller's frame-
        // boundary drain. The transform apply's changed entities are appended to
        // changed_entities. Routing every command kind through this ONE method is
        // what stops a kind from ever again being silently dropped by a pass that
        // forgot to handle it -- the structural fix behind the #252 audit's
        // finding #3 (a self-reset's host-handled commands were dropped).
        //
        // Behavior-preserving scope (#256 seam A): the MAIN per-frame drain routes
        // here. The narrower passes keep their deliberately limited application
        // until a separate, semantics-visible change widens them -- the self.start
        // pass (rebuild_behavior_scene) and the WZ_EVENT_SCENE_LOADED pass apply
        // only their own subset, and the async SPAWN_COMPLETED pass stays
        // transforms-only (a host-state reset belongs on the pool manager's
        // acquire, not the async completion; #252 audit). No-op if no behavior
        // scene is live (the caller's guard already holds on the main path).
        void apply_all_behavior_commands(
            const std::vector<wz::engine::behavior::BehaviorCommand>& commands,
            std::vector<wz::scene::RuntimeEntityId>&                  changed_entities,
            std::vector<DeferredSpawnRequest>&                        out_spawn_requests);

        // ─── dispatch_scene_behaviors phases (#256 seam B) ────────────────────
        // dispatch_scene_behaviors reads as a short sequence over these; each is a
        // private phase with a named responsibility. All assume a live
        // behavior_scene_ (the orchestrator's early-out guard holds it).

        // Phase 1: recompute behavior_scene_->entity_active (the hierarchical "live?"
        // mask) from the authored scene, fire WZ_EVENT_SELF_ACTIVATED on each
        // parked->live rising edge, and roll prev_active_by_id_ forward. Returns the
        // commands the activation handlers produced -- empty unless a self.activated
        // subscriber exists AND a prior frame is on record -- for the dispatch phase
        // to seed into the frame's command buffer (same frame, no id staleness).
        // The mask recompute + prev_active roll always run (collision reads the mask,
        // and rolling keeps a paused->resumed dispatch edge-free); fire_edges gates
        // ONLY the SELF_ACTIVATED firing, so behaviors OFF fires no behavior code.
        [[nodiscard]] std::vector<wz::engine::behavior::BehaviorCommand>
        compute_active_mask_and_fire_edges(bool fire_edges);

        // Phase 2: populate frame_storage_'s collision, proximity, and input-event
        // tables from the current scene + this frame's input, so the dispatch +
        // constraint phases route real events.
        void build_frame_event_tables(const wz::input::InputState& input);

        // Phase 3 (has-behaviors only): seed the frame command buffer with
        // activation_commands, dispatch the frame behaviors + the self-paced cognition
        // tick, then apply the produced buffer via apply_all_behavior_commands --
        // appending its transform-changed entities to changed_entities, filling the
        // deferred-authoring sink, and collecting the frame-boundary prefab spawns.
        void dispatch_behaviors_and_apply(
            const wz::input::InputState& input,
            float dt,
            const std::vector<wz::engine::behavior::BehaviorCommand>&
                activation_commands,
            std::vector<wz::scene::RuntimeEntityId>& changed_entities,
            wz::engine::behavior::BehaviorAuthoringBuffer& authoring,
            std::vector<DeferredSpawnRequest>& spawn_requests);

        // Phase 4: integrate motion + snap terrain constraints (appending their
        // changed entities to changed_entities), dedup, then re-propagate the polytree
        // if anything moved.
        void integrate_motion_and_constraints(
            float dt, std::vector<wz::scene::RuntimeEntityId>& changed_entities);

        // Phase 5: drain the behavior-issued deferred-authoring buffer (#204) --
        // spawn-child / remove / set-renderable / reparent / add+remove-component --
        // each through the host's own apply method. Frame-boundary: a structural entry
        // may rebuild the behavior runtime, so this runs after every behavior_scene_
        // read this tick.
        void drain_deferred_authoring(
            const wz::engine::behavior::BehaviorAuthoringBuffer& authoring);

        // Phase 6: drain the fire-and-forget SPAWN_PREFAB requests collected during
        // the command apply (each grafts a subtree + rebuilds the runtime).
        void drain_prefab_spawns(
            const std::vector<DeferredSpawnRequest>& spawn_requests);

        // Phase 7: drain the spawn-with-identity buffer (#252 pooling) -- materialize
        // each pending spawn, then dispatch its SPAWN_COMPLETED/_FAILED back to the
        // spawner against the final runtime and apply the completion handlers'
        // transform commands (host-handled kinds deliberately excluded; #252 audit).
        void drain_identity_spawns();

        wz::engine::AppContext&                  ctx_;
        wz::engine::rendering::RhiSceneRenderer  renderer_;
        uint32_t                                 graph_epoch_ = 0;  // last bound


        // View/camera unit (#258 avenue 4). Owns the free-fly edit/standalone
        // camera, projection params + aspect, the single active view render_scene
        // consumes, the camera-source policy (free-fly vs. a selected scene
        // camera), and the scene-camera selection anchor. This app is its host: it
        // feeds view_ input (update_free_fly), the selected camera's params
        // (apply_scene_active_camera -> view_.select_scene_camera) and per frame
        // the selected node's live world matrix (materialize_active_view), then
        // reads back view_.active_view() in render_scene. See
        // engine/app/view_controller.h.
        ViewController view_{};

        // The current graph draft (kept for the renderable_asset_node_id -> key
        // bridge) and the loaded scene's nodes (with the bridged renderable_asset).
        wz::asset::AssetGraphDraft                       graph_draft_{};

        // #221 OWNERSHIP CONTRACT — document_ (a SceneDocument, see
        // scene_document.h) holds the AUTHORED / SERIALIZED node array, the settled
        // single source of truth for the scene's NON-transform authoring data:
        //   - SINGLE WRITER: the edit verbs (set_node_*, add/remove_node_*,
        //     graft/spawn, load). The simulation NEVER mutates document_.nodes().
        //     There are no parallel per-frame runtime copies of these fields — that
        //     is what keeps them sync-free.
        //   - TRANSFORM / HIERARCHY are NOT owned here: their live truth is the
        //     simulation polytree (behavior_scene_->storage.polytree). Reads go
        //     through scene_world_transforms() / node_world_transform(); the ONE
        //     edit seam is apply_node_local_transform(); the authored .local /
        //     .parent_id fields are refreshed from the polytree only on demand
        //     (derived_authored_transform, i.e. derive-on-save), never per frame.
        //   - NON-transform fields (visible, renderable_asset keys, camera params,
        //     scene_source, audio anchors) are read per-frame BY DESIGN as document
        //     data — the renderer consumes visible + renderable_asset fresh each
        //     frame; the sim never writes them, so the document IS their truth.
        //   - COMPONENTS (collision/motion/audio/…) are PROJECTED into the
        //     SceneInstance at rebuild_behavior_scene; a per-frame field tweak that
        //     already has a live record patches BOTH the authored field here (so
        //     save persists) AND the runtime record in place (so no rebuild).
        // document_.grafted_ids() tracks nodes grafted from a scene_source
        // reference (#213 instance mode): runtime-only children appended to
        // document_.nodes() by graft_scene_sources, so a re-graft drops the prior
        // graft and save_scene excludes them (not populated for flatten — those
        // become authored). document_.dirty() is the unsaved-edit flag.
        SceneDocument document_{};

        // Source scene file, for save_scene (persist live edits).
        wz::fs::Path  scene_source_path_{};

        // The asset-graph + behavior-module paths the last load_scene used, so
        // open_scene can swap the WORKING SCENE (e.g. to a scenelet for in-editor
        // prefab editing) while reusing the same project asset graph + modules.
        wz::fs::Path  asset_graph_path_{};
        wz::fs::Path  behavior_module_folder_{};

        // The project's MAIN scene (the first one load_scene loaded). Preserved when
        // open_scene swaps to a scenelet, so open_scene("") reopens it -- the prefab
        // editor's "back to the scene" without the caller tracking the path.
        wz::fs::Path  main_scene_path_{};

        // Behavior runtime: the load -> register -> initialize -> per-frame
        // dispatch -> apply-command-buffer sequence, hosted inside this shared
        // runtime so a scene's behavior bindings execute in both the editor
        // viewport and a shipped app.
        //   - registry_ / plugins_ own the registered modules (built-ins + the
        //     project DLLs loaded from behavior_module_folder).
        //   - behavior_scene_ is the runtime SceneInstance materialized from the
        //     authored document_.nodes(); behaviors read its polytree and the command
        //     apply mutates it, then changed transforms are written back to
        //     document_.nodes() (the renderer's source of truth).
        //   - frame_storage_ holds the per-frame behavior command buffer; reused
        //     each tick (the shared wz::engine::FrameStorage).
        wz::engine::behavior::BehaviorRegistry   registry_{};
        wz::engine::behavior::BehaviorPluginHost plugins_{};
        wz::engine::FrameStorage                 frame_storage_{};
        // Behavior-defined events (v34): persists across frames (double-buffered), so
        // an event a behavior emits one frame reaches an `event` trigger the next.
        wz::engine::behavior::BehaviorEventBuffer behavior_events_{};
        std::optional<wz::engine::assets::SceneInstance> behavior_scene_{};
        uint64_t                                 behavior_frame_index_ = 0;
        // Absolute accumulated sim-time (seconds) for the self-paced cognition.tick
        // scheduler: the monotonic clock dispatch_cognition_tick compares scheduled
        // wakes against (the per-frame FrameContext only carries this frame's delta).
        double                                   behavior_sim_time_ = 0.0;

        // Motion Filter per-node state (secondary-motion camera damping), keyed by
        // STABLE authored id so the smoothing survives scene rebuilds/spawns (like
        // prev_active_by_id_). apply_motion_filters advances it as the last
        // transform step each tick; a node absent here seeds on first sight.
        std::unordered_map<
            wz::scene::AuthoredEntityId,
            wz::engine::motion::MotionFilterState> motion_filter_states_{};

        // --- WZ_EVENT_SELF_ACTIVATED edge tracking (#252 pooling) -----------------
        // Last frame's effective-active mask keyed by STABLE authored id (runtime
        // ids renumber on rebuild, so the edge must be diffed by id). A node whose
        // effective active goes 0 -> 1 (an external unpark) fires SELF_ACTIVATED so
        // a reused pool instance self-resets. Absent-in-prev is treated as live, so
        // birth (self.start) does not masquerade as an activation edge.
        std::unordered_map<std::string, std::uint8_t> prev_active_by_id_{};
        // Cached on rebuild: does any behavior subscribe to self.activated? Skips
        // the per-frame edge scan entirely when nothing listens (zero-cost unused).
        bool                                     has_self_activated_subscriber_ = false;

        // Spawn-with-identity sink (#252 pooling). A member (not per-frame) because
        // submit can fire from self.start (in rebuild_behavior_scene) as well as
        // frame.update; both contexts point here and dispatch_scene_behaviors drains
        // it each frame (swap-out first, so a spawn's own self.start submit lands
        // NEXT frame rather than re-entering the drain).
        wz::engine::behavior::BehaviorSpawnBuffer spawn_identity_buffer_{};

        // Scene-simulation start/stop gates (#258), read by dispatch_scene_behaviors.
        // See set_simulation_enabled / set_behaviors_enabled for the semantics. Both
        // default ON so normal play + edit and every existing test are unchanged.
        bool                                     simulation_enabled_ = true;
        bool                                     behaviors_enabled_  = true;

        // --- Per-frame rebuild profiling (issue #252) -----------------------------
        // Counted during dispatch_scene_behaviors, reset each simulation_tick. More
        // than one rematerialize/rebuild in a single frame is redundant structural
        // work (warned). Samples accumulate into a CSV via the data_table +
        // csv_export asset chain at save/shutdown, for before/after Track-A analysis.
        uint32_t rematerialize_count_this_frame_ = 0;
        uint32_t rebuild_scene_count_this_frame_ = 0;
        struct FrameProfileSample
        {
            uint64_t frame = 0;
            double   dt_ms = 0.0;
            double   sim_ms = 0.0;
            uint64_t scene_nodes = 0;
            uint32_t rematerialize = 0;
            uint32_t rebuild = 0;
            std::string callers{};  // ";"-joined seam labels for the remat calls
        };
        // Opt-in (issue #252 follow-up): off by default so a normal editor/play
        // session records nothing and writes no CSV. Toggled from the editor menu
        // via set_frame_profiling_enabled.
        bool                                     frame_profiling_enabled_ = false;
        std::vector<FrameProfileSample>          frame_profile_{};
        // A wall-clock tag (YYYYMMDD_HHMMSS) minted once per process so each play
        // session writes its OWN frame_profile_<tag>.csv -- successive play/stop
        // cycles are separate host processes and no longer clobber one file (#252).
        std::string                              frame_profile_run_tag_{};
        // Per-frame ";"-joined short caller labels for each rematerialize call,
        // reset each sim tick -> the frame_profile "remat_callers" column names
        // WHICH seam forced the spurious burst (#252).
        std::string                              remat_callers_this_frame_{};

        // Runtime prefab spawning (the second prefab-system milestone). Prefabs are
        // registered scenelets keyed by their name's FNV-1a/32 hash (register_prefab);
        // a SPAWN_PREFAB behavior command resolves the hash here, clones the nodes
        // with conflict-free ids, and grafts them. spawn_counter_ feeds the per-
        // instance id remap (instantiate_prefab_nodes) so every spawn gets a
        // disjoint id namespace; it only increments, so it is unique for the app's
        // lifetime.
        std::unordered_map<
            uint32_t,
            std::vector<wz::engine::assets::SceneNodeAsset>> prefab_by_hash_{};
        uint32_t                                 spawn_counter_ = 0;

        // Name + path of each scenelet found by register_scenelet_prefabs, for the
        // editor's scenelet menu (the read-only catalog behind scenelet_catalog()).
        std::vector<SceneletCatalogEntry>        scenelet_catalog_{};

        // Play-mode audio runtime: owns the realtime scheduler and (when started)
        // the output device. Started lazily on the first play-mode scene load and
        // stopped when leaving play; the editor leaves it idle.
        wz::engine::audio::AudioRuntime          audio_runtime_{};

        // Stable owner of grain-cloud descriptors posted to the audio thread (a
        // PlayGrainCloud command carries a pointer into this). Cleared on scene
        // load after the runtime is (re)started.
        wz::engine::audio::GrainCloudDescStore    grain_desc_store_{};

        // AudioSource client ids already auto-played this scene. start_scene_audio()
        // clears + fills it; start_spawned_audio() consults + extends it so a spawned
        // subtree's auto_play sources start exactly once and the ambient bed is never
        // double-played. Client ids are stable per node, so this survives the
        // behavior-scene rebuild a spawn triggers.
        std::unordered_set<uint32_t>              auto_played_clients_{};

        // Per-scene audio-spatialization state (prev positions for Doppler).
        // Cleared on scene load next to grain_desc_store_; driven each
        // simulation_tick (play mode only) by update_scene_audio_spatialization.
        wz::engine::audio::AudioSpatializationState audio_spatialization_{};
    };
}
