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
#include <engine/rendering/rhi_scene_renderer.h>

#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/audio/audio_runtime.h>
#include <engine/audio/scene_audio.h>
#include <engine/behavior/behavior_plugin_adapter.h>
#include <engine/behavior/behavior_registry.h>
#include <engine/frame_storage.h>

#include <asset/dag.h>
#include <asset/draft.h>
#include <bench/flying_camera.h>
#include <file/filesystem.h>
#include <input/input.h>
#include <math/mat4.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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
        // `id` in the in-memory scene, returning false if no node has that id.
        // This is the apply behind the editor's live transform preview — the
        // next render_scene() draws the node at the new transform with no GPU
        // rebuild (scene_nodes_ is consumed fresh each frame). Persisting the
        // edit to the scene file on disk is a separate path.
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

        // ─── Live behavior-binding authoring ─────────────────────────────────
        // Apply behind the host ABI's behavior verbs. Each mutates the matching
        // node's behavior binding(s) in scene_nodes_, marks the scene dirty, and
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
        // in-memory scene (scene_nodes_), and mark the scene dirty on success.
        // The renderer consumes scene_nodes_ fresh each frame, so the next
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
        // Marks the scene dirty on success; the renderer reads scene_nodes_ each
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
        // children appear under the host immediately (renderer reads scene_nodes_
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
        // how strongly it aligns to the surface normal. Rebuilds the runtime
        // scene so integrate_motion + apply_terrain_constraints see the change;
        // marks the scene dirty. False (logged no-op) if the node is missing.
        bool set_node_motion_terrain_fields(
            const wz::scene::AuthoredEntityId& node_id,
            bool terrain_constrained,
            float ride_height,
            float footprint_radius,
            bool align_to_surface,
            float alignment_strength);

        // Flatten the node's referenced Scene asset into the live scene (#213):
        // resolve the node's scene_source, expand its GLB-named nodes as real,
        // persistent children of the node in scene_nodes_ (id "<host>/<glbname>",
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
        // scene_nodes_ without a snapshot (mirrors node_has_component). Counts
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
        // scene_nodes_ — valid only until the next scene mutation.
        [[nodiscard]] const wz::engine::assets::SceneGLBSceneSource*
        node_glb_scene_source(
            const wz::scene::AuthoredEntityId& node_id) const;

        // The node's current Collision component (issue #216/#217), or nullptr if
        // the node is missing or has none. Lets the collision-authoring test
        // observe set_node_collision_asset's effect (the bridged collision_asset
        // key + constrain_movement) without a snapshot. The pointer is into
        // scene_nodes_ — valid only until the next scene mutation.
        [[nodiscard]] const wz::engine::assets::SceneCollisionAsset*
        node_collision(const wz::scene::AuthoredEntityId& node_id) const;

        // The node's current Motion component (issue #216/#217), or nullptr if
        // the node is missing or has none. Lets the motion-terrain test observe
        // set_node_motion_terrain_fields without a snapshot. Same pointer-lifetime
        // caveat as node_collision.
        [[nodiscard]] const wz::engine::assets::SceneMotionAsset*
        node_motion(const wz::scene::AuthoredEntityId& node_id) const;

        // A COPY of just the runtime-grafted scene nodes (issue #213): the
        // entries of scene_nodes_ whose id is in grafted_node_ids_ — the
        // instance-mode sub-trees appended by graft_scene_sources. These live
        // only in the live runtime (an instanced scene_source re-imports from its
        // reference; the children are never persisted as authored nodes), so the
        // editor's JSON-sourced scene tree never has them. The host ABI surfaces
        // this (as a scene snapshot) so the editor can merge the grafts under
        // their hosts. Each entry keeps its parent_id (the host's id, or a deeper
        // graft's id) so the merger can place it. Engine-thread only (reads
        // scene_nodes_); the editor reaches it through the blocking
        // request_grafted_scene_nodes handshake. Empty when nothing is grafted.
        [[nodiscard]] std::vector<wz::engine::assets::SceneNodeAsset>
        grafted_scene_nodes() const;

        // Persist the current scene back to its source file: the nodes are
        // re-emitted, all other scene data preserved. No-op (returns true) when
        // no live edit happened since load/last save; false on write failure.
        bool save_scene();

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

        // Current authored local translation of the live scene node with `id`,
        // or std::nullopt if no node has that id. Reflects behavior command
        // write-back (a behavior that moves a node shows here after a
        // simulation_tick), so diagnostics + the behavior dispatch test can
        // observe the effect on scene_nodes_ without a render.
        [[nodiscard]] std::optional<wz::math::Vec3> node_local_translation(
            const wz::scene::AuthoredEntityId& id) const;

        // Current authored local scale of the live scene node with `id`. Like
        // node_local_translation, reflects the per-frame sim write-back, so a
        // test can confirm a scale edit on a sim-driven (e.g. terrain
        // constrained) node survives a simulation_tick (#218 follow-up).
        [[nodiscard]] std::optional<wz::math::Vec3> node_local_scale(
            const wz::scene::AuthoredEntityId& id) const;

    private:
        // Materialize the single active view (view-projection + world position)
        // render_scene draws with, from the currently selected camera source.
        // Called once per simulation_tick after both sources are current (the
        // free-fly camera was updated from input and behaviors moved the scene
        // camera node). The Scene source reads the camera node's live world
        // transform; if it can't be resolved this frame (e.g. mid-rebuild) the
        // previous active view is kept rather than dropping to free-fly, so a
        // transient invalid handle cannot flip the camera. No per-frame branch
        // or validity guard survives into the render path.
        void update_active_view();

        // Load every behavior-module DLL in `module_folder` and register its
        // modules into registry_. No-op for an empty/missing folder. Reusable
        // across a scene/project swap (the plugin host reloads existing modules).
        void load_behavior_modules(const wz::fs::Path& module_folder);

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
        std::size_t assemble_render_bindings(
            const wz::asset::AssetGraphDraft& draft);

        // Re-assemble renderable bindings after one was edited live (issue #213
        // increment 2): re-bridge the pre-built renderables, re-run
        // assemble_render_bindings against the bound graph, then compile the
        // freshly created renderables (commit + resolve_all). The renderer reads
        // scene_nodes_ each frame, so the next render reflects it. No-op without
        // an asset library.
        void rematerialize_render_bindings();

        // Re-materialize the GLB scene-source descriptors after one was edited
        // (issue #213): re-resolve every descriptor into a Scene asset, compile
        // the freshly registered assets (commit + resolve_all), then re-graft the
        // hosts' children and rebuild the behavior runtime. The shared sequence
        // set_node_glb_scene_source and the style mutators run after mutating a
        // descriptor; factored out so the style edits reuse the exact 3a path.
        // No-op without an asset library (resolve/graft are guarded).
        void rematerialize_glb_scene_sources();

        // Graft every scene_source reference's sub-scene into scene_nodes_ as
        // children of its host (issue #213, instance mode). For each host node
        // carrying a resolved scene_source key, resolve the referenced Scene
        // asset and append its expanded GLB-named children (via
        // expand_scene_source_children) so they render (scene_nodes_ is the
        // renderer's source of truth) and are behavior-addressable. Idempotent
        // within a load: previously grafted children (id "<host>/...") are
        // removed first, so a re-bind/re-resolve re-grafts cleanly. Returns the
        // number of children grafted. Caller rebuilds the behavior scene after.
        std::size_t graft_scene_sources();

        // Mirror a grafted scene-source child's authored components onto its host
        // as a sticky override (issue #213), so an edit to a runtime-only grafted
        // child (which save_scene excludes) survives reload. No-op when child_id
        // is not a grafted node. Keyed by the child's sub-scene id (the "<host>/"
        // suffix); upserts the override from the child's current authored state,
        // erasing it when the child carries no overridable component. Re-applied
        // to the expanded child by graft_scene_sources on the next (re)graft.
        void capture_grafted_child_override(
            const wz::scene::AuthoredEntityId& child_id);

        // Materialize the live authored scene (scene_nodes_) into a runtime
        // SceneInstance and (re)initialize its behaviors against the registry.
        // Called from load_scene and after any structural scene edit so the
        // behavior runtime tracks the authored scene. Clears behavior_scene_
        // when there are no behavior bindings (nothing to run).
        void rebuild_behavior_scene();

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

        // Apply a SET_ACTIVE_CAMERA command: resolve the runtime entity to its
        // authored node and record it as the scene-camera selection anchor
        // (id + live polytree handle + SceneCameraAsset params). Flips
        // camera_source_ to Scene only when prefer_scene_camera_ is set (play);
        // in the editor the anchor is recorded but the free-fly source stays
        // active. The view itself is built later by update_active_view().
        void apply_scene_active_camera(wz::scene::RuntimeEntityId runtime_entity);

        // Re-point active_camera_entity_ at active_camera_id_'s runtime entity in
        // the current behavior scene. Called once after each rebuild (NOT per
        // frame), so the Scene camera source survives a behavior-scene rebuild
        // (e.g. a behavior spawning a child). No-op when no camera is selected or
        // the node is gone.
        void refresh_active_camera_entity();

        // One behavior tick: propagate transforms, dispatch frame/input events,
        // apply the produced command buffer + integrate motion, then write the
        // changed node transforms back into scene_nodes_ so the next
        // render_scene() draws them. No-op when no behavior scene is live.
        void dispatch_scene_behaviors(
            const wz::input::InputState& input, float dt);

        wz::engine::AppContext&                  ctx_;
        wz::engine::rendering::RhiSceneRenderer  renderer_;
        uint32_t                                 graph_epoch_ = 0;  // last bound


        // The app's own free-fly camera (game-app parity), and the editor's edit
        // camera: updated from input in simulation_tick (when the host arms
        // drive_camera) and used as the FreeFly camera source. Projection params
        // mirror the scene camera defaults; aspect tracks the window reported by
        // the latest input.
        wz::bench::FlyingCamera        camera_{};
        float                          camera_fov_y_ = 1.0472f;       // ~60 deg
        float                          camera_near_  = 0.1f;
        float                          camera_far_   = 100000.0f;
        float                          aspect_       = 1280.0f / 720.0f;

        // The single active view rendering consumes -- materialized each
        // simulation_tick by update_active_view() from the selected source. The
        // render path references THIS and nothing else (no override, no
        // per-frame scene-tree lookup, no fallback branch).
        struct ActiveView
        {
            wz::math::Mat4 view_projection = wz::math::Mat4::identity();
            wz::math::Vec3 world_position{};   // clipmap lattice snap reads this
        };
        ActiveView active_view_{};

        // Which source update_active_view() materializes from. FreeFly = the
        // editor/standalone fly-cam (camera_); Scene = the selected scene-authored
        // camera (active_camera_* below). Defaults to FreeFly; flipped to Scene
        // only when a scene camera is selected AND prefer_scene_camera_ is set.
        enum class CameraSource { FreeFly, Scene };
        CameraSource camera_source_ = CameraSource::FreeFly;

        // Diagnostic (#219): whether the Scene source resolved its handle the
        // last time update_active_view() ran, so it logs only the transition
        // edges (a flip to/from "holding last view") rather than per frame.
        bool scene_source_resolved_ = false;

        // Host policy (set_prefer_scene_camera): standalone/play sets this true so
        // a selected scene camera becomes active on load; the editor leaves it
        // false so the free-fly edit camera stays active.
        bool prefer_scene_camera_ = false;

        // Active scene-camera SELECTION ANCHOR, recorded when a scene-setup
        // behavior selects it on WZ_EVENT_SCENE_LOADED (SET_ACTIVE_CAMERA).
        // active_camera_id_ is the stable anchor; active_camera_entity_ is its
        // live polytree handle, re-resolved only when the behavior scene is
        // rebuilt (never per frame) so the Scene source survives a rebuild.
        // Recorded regardless of camera_source_, so the editor can later switch
        // the source to Scene to preview. Projection params are captured at
        // selection (a reload re-reads them).
        wz::scene::AuthoredEntityId          active_camera_id_{};
        wz::scene::RuntimeEntityId           active_camera_entity_ =
            wz::scene::INVALID_RUNTIME_ENTITY;
        wz::engine::assets::SceneCameraAsset active_camera_params_{};

        // The current graph draft (kept for the renderable_asset_node_id -> key
        // bridge) and the loaded scene's nodes (with the bridged renderable_asset).
        wz::asset::AssetGraphDraft                       graph_draft_{};
        std::vector<wz::engine::assets::SceneNodeAsset>  scene_nodes_{};

        // Ids of scene nodes currently grafted from a scene_source reference
        // (issue #213, instance mode). These are runtime-only children appended
        // to scene_nodes_ by graft_scene_sources; tracked so a re-graft can drop
        // the previous graft cleanly and so save_scene can exclude them (an
        // instanced sub-scene re-imports from the reference, it is not persisted
        // as authored nodes). NOT populated for flatten (those become authored).
        std::vector<wz::scene::AuthoredEntityId>         grafted_node_ids_{};

        // Source scene file + a dirty flag, for save_scene (persist live edits).
        wz::fs::Path  scene_source_path_{};
        bool          scene_dirty_ = false;

        // Behavior runtime. This is the same load -> register -> initialize ->
        // per-frame dispatch -> apply-command-buffer sequence the standalone
        // game_app runs, hosted inside this shared runtime so a scene's behavior
        // bindings execute in both the editor viewport and a shipped app.
        //   - registry_ / plugins_ own the registered modules (built-ins + the
        //     project DLLs loaded from behavior_module_folder).
        //   - behavior_scene_ is the runtime SceneInstance materialized from the
        //     authored scene_nodes_; behaviors read its polytree and the command
        //     apply mutates it, then changed transforms are written back to
        //     scene_nodes_ (the renderer's source of truth).
        //   - frame_storage_ holds the per-frame behavior command buffer; reused
        //     each tick (matching game_app's FrameStorage).
        wz::engine::behavior::BehaviorRegistry   registry_{};
        wz::engine::behavior::BehaviorPluginHost plugins_{};
        wz::engine::FrameStorage                 frame_storage_{};
        std::optional<wz::engine::assets::SceneInstance> behavior_scene_{};
        uint64_t                                 behavior_frame_index_ = 0;

        // Play-mode audio runtime: owns the realtime scheduler and (when started)
        // the output device. Started lazily on the first play-mode scene load and
        // stopped when leaving play; the editor leaves it idle.
        wz::engine::audio::AudioRuntime          audio_runtime_{};

        // Stable owner of grain-cloud descriptors posted to the audio thread (a
        // PlayGrainCloud command carries a pointer into this). Cleared on scene
        // load after the runtime is (re)started.
        wz::engine::audio::GrainCloudDescStore    grain_desc_store_{};

        // Per-scene audio-spatialization state (prev positions for Doppler).
        // Cleared on scene load next to grain_desc_store_; driven each
        // simulation_tick (play mode only) by update_scene_audio_spatialization.
        wz::engine::audio::AudioSpatializationState audio_spatialization_{};
    };
}
