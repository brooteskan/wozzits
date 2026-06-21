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

        // rhi render-program / shader-module registry occupancy. Both reset to 0
        // on a graph swap (the outgoing graph's programs/shaders are retired) so
        // the fixed-capacity registries stay bounded across editor rebinds.
        [[nodiscard]] std::size_t registered_program_count() const;
        [[nodiscard]] std::size_t registered_shader_count() const;

        // SRV descriptor tables cached by the renderer's command recorder; resets
        // to 0 on a graph swap so descriptor-heap ranges don't leak across binds.
        [[nodiscard]] std::size_t cached_descriptor_table_count() const;

    private:
        // The view-projection render_scene draws with: the override if set,
        // otherwise built from the free-fly camera + projection params + aspect.
        wz::math::Mat4 compute_view_projection() const;

        wz::engine::AppContext&                  ctx_;
        wz::engine::rendering::RhiSceneRenderer  renderer_;
        uint32_t                                 graph_epoch_ = 0;  // last bound

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
    };
}
