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

#include <asset/dag.h>
#include <asset/draft.h>
#include <bench/flying_camera.h>
#include <file/filesystem.h>
#include <input/input.h>
#include <math/mat4.h>

#include <cstddef>
#include <cstdint>
#include <optional>
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

        // Persist the current scene back to its source file: the nodes are
        // re-emitted, all other scene data preserved. No-op (returns true) when
        // no live edit happened since load/last save; false on write failure.
        bool save_scene();

        // Per-frame operations. The caller owns the loop and the device-frame
        // boundaries (begin_frame/clear/end_frame/present). simulation_tick takes
        // the frame's input + dt so the app drives its own free-fly camera (the
        // game-app-parity runtime camera); pass a default-constructed InputState
        // to tick with no input.
        void simulation_tick(const wz::input::InputState& input, float dt);
        bool render_scene();      // record scene draws (between begin/end frame)

        // Editor override: while set, render_scene uses this view-projection
        // instead of the app's free-fly camera, letting an editor drive the view
        // without the app owning editor input. Clearing it returns control to the
        // app camera.
        void set_camera_override(const wz::math::Mat4& view_projection);
        void clear_camera_override();

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

    private:
        // The view-projection render_scene draws with: the override if set,
        // otherwise built from the free-fly camera + projection params + aspect.
        wz::math::Mat4 compute_view_projection() const;

        wz::engine::AppContext&                  ctx_;
        wz::engine::rendering::RhiSceneRenderer  renderer_;
        uint32_t                                 graph_epoch_ = 0;  // last bound


        // TODO: The app should not prefer its own free-fly camera. The editor should be
        // able to use the free fly camera in place of the camera defined in the scene
        // the app should use the camera in the scene first. if no camera is defined in
        // the scene then it should use this free fly camera. 
        // 
        // The app's own free-fly camera (game-app parity): updated from input in
        // simulation_tick and used to build the view-projection unless an editor
        // override is set. Projection params mirror the scene camera defaults;
        // aspect tracks the window reported by the latest input.
        wz::bench::FlyingCamera        camera_{};
        float                          camera_fov_y_ = 1.0472f;       // ~60 deg
        float                          camera_near_  = 0.1f;
        float                          camera_far_   = 100000.0f;
        float                          aspect_       = 1280.0f / 720.0f;
        std::optional<wz::math::Mat4>  camera_override_{};

        // The current graph draft (kept for the renderable_asset_node_id -> key
        // bridge) and the loaded scene's nodes (with the bridged renderable_asset).
        wz::asset::AssetGraphDraft                       graph_draft_{};
        std::vector<wz::engine::assets::SceneNodeAsset>  scene_nodes_{};

        // Source scene file + a dirty flag, for save_scene (persist live edits).
        wz::fs::Path  scene_source_path_{};
        bool          scene_dirty_ = false;
    };
}
