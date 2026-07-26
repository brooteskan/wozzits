#pragma once

// engine/app/view_controller.h
//
// ViewController — the per-host view/camera unit extracted from WozzitsApp_v1
// (issue #258, avenue 4: "thin WozzitsApp_v1 to a composition root"). It owns
// the free-fly edit/standalone camera, the projection params, the single active
// view render_scene consumes, and the camera-SOURCE policy (the free-fly camera
// vs. a selected scene-authored camera).
//
// It is a PURE view/policy unit: it knows nothing about the behavior runtime, the
// scene document, or the audio device. The host feeds it input (update_free_fly),
// the selected camera's params (select_scene_camera), and — per frame — the
// selected camera node's live world matrix (update_active_view), then reads back
// the materialized view (active_view). That inversion is the seam: the app keeps
// the reaction (behavior dispatch, scene-node lookup, entity resolution) and this
// unit keeps the mechanism (the camera math + source policy).
//
// WozzitsApp_v1 composes it in both roles (editor viewport + standalone play),
// which differ only in the prefer_scene_camera policy knob.

#include <engine/assets/scene/scene_asset_data.h>   // SceneCameraAsset

#include <bench/flying_camera.h>
#include <input/input.h>
#include <logging/logger.h>
#include <math/mat4.h>
#include <math/math_types.h>
#include <scene/scene_ecs.h>                         // Authored/RuntimeEntityId

#include <cstddef>
#include <optional>

namespace wz::app
{
    class ViewController
    {
    public:
        // The single active view rendering consumes — materialized each frame by
        // update_active_view() from the selected source. The render path
        // references THIS and nothing else (no override, no per-frame scene-tree
        // lookup, no fallback branch).
        struct ActiveView
        {
            wz::math::Mat4 view_projection = wz::math::Mat4::identity();
            wz::math::Vec3 world_position{};   // clipmap lattice snap reads this
        };

        // --- free-fly edit/standalone camera ---------------------------------
        // The app's own free-fly camera (game-app parity) and the editor's edit
        // camera. Projection params mirror the scene-camera defaults; aspect
        // tracks the host window. Exposed by reference so the host can seed/persist
        // the pose (editor viewport metadata) without this unit knowing about I/O.
        wz::bench::FlyingCamera&       free_fly_camera()       { return camera_; }
        const wz::bench::FlyingCamera& free_fly_camera() const { return camera_; }

        void  set_aspect(float aspect) { aspect_ = aspect; }
        float aspect() const           { return aspect_; }

        // Advance the free-fly camera from input; returns true when the pose
        // actually moved. In editor mode (!prefer_scene_camera) a move marks the
        // editor-camera-dirty flag: the moved pose is unsaved viewport state.
        bool update_free_fly(const wz::input::InputState& input, float dt);

        // --- camera-source policy --------------------------------------------
        // Standalone/play sets prefer_scene_camera true so a selected scene camera
        // becomes the active source on load; the editor leaves it false so the
        // free-fly edit camera stays active and you can navigate even when the
        // scene authors a camera (the selection anchor is still recorded, so a
        // future "look through scene camera" editor toggle is a cheap source flip).
        // Re-evaluates the active SOURCE, not just the preference. Setting this
        // used to only matter if it was already true when a camera was selected,
        // so flipping it afterwards silently did nothing and the view stayed on
        // the free-fly camera -- an ordering trap with no symptom except an
        // empty frame (#301).
        void set_prefer_scene_camera(bool prefer);
        bool prefer_scene_camera() const          { return prefer_scene_camera_; }
        bool scene_source_active() const   { return source_ == CameraSource::Scene; }

        // --- scene-camera selection anchor -----------------------------------
        // Record the selected scene camera (stable authored id + live runtime
        // entity + projection params captured at selection). Flips the active
        // source to Scene only when prefer_scene_camera is set (play); the editor
        // records the anchor but stays on the free-fly camera. Logs the selection.
        void select_scene_camera(
            const wz::scene::AuthoredEntityId& id,
            wz::scene::RuntimeEntityId entity,
            const wz::engine::assets::SceneCameraAsset& params,
            wz::Logger& logger);

        // Drop the anchor and fall back to the free-fly source (scene (re)load).
        void clear_scene_camera();

        // Re-seat the live entity handle after a behavior-scene rebuild renumbers
        // the polytree; the id anchor is unchanged. Host passes INVALID when the
        // anchored id no longer resolves. Keeps the Scene source alive across a
        // rebuild (e.g. a behavior spawning a child) without a per-frame lookup.
        void set_active_camera_entity(wz::scene::RuntimeEntityId entity)
        { active_camera_entity_ = entity; }

        const wz::scene::AuthoredEntityId& active_scene_camera_id() const
        { return active_camera_id_; }
        wz::scene::RuntimeEntityId active_camera_entity() const
        { return active_camera_entity_; }
        bool has_scene_camera() const { return !active_camera_id_.empty(); }

        // --- active-view materialization -------------------------------------
        // Materialize the active view from the selected source. For the Scene
        // source, scene_camera_world is the selected node's live world matrix
        // (std::nullopt = the handle could not be resolved this frame, e.g.
        // mid-rebuild → hold the previous view rather than dropping to free-fly,
        // so a transient invalid handle cannot flip the camera). node_count /
        // scene_live feed the resolve-edge diagnostic (#219). For the FreeFly
        // source the scene arguments are ignored.
        void update_active_view(
            const std::optional<wz::math::Mat4>& scene_camera_world,
            std::size_t node_count,
            bool scene_live,
            wz::Logger& logger);
        const ActiveView& active_view() const { return active_view_; }

        // --- editor viewport dirty (unsaved free-fly pose) -------------------
        // The editor viewport free-fly pose is unsaved state INDEPENDENT of the
        // scene's own dirty flag (moving the camera is not a scene edit), so the
        // host persists scene_editor_metadata.camera when this is set. Set by
        // update_free_fly in editor mode; cleared by the host on load / save.
        bool editor_camera_dirty() const { return editor_camera_dirty_; }
        void clear_editor_camera_dirty()  { editor_camera_dirty_ = false; }

    private:
        // Which source update_active_view() materializes from. FreeFly = the
        // editor/standalone fly-cam (camera_); Scene = the selected scene-authored
        // camera. Defaults to FreeFly; flipped to Scene only when a scene camera is
        // selected AND prefer_scene_camera_ is set.
        enum class CameraSource { FreeFly, Scene };

        wz::bench::FlyingCamera camera_{};
        float camera_fov_y_ = 1.0472f;       // ~60 deg
        float camera_near_  = 0.1f;
        float camera_far_   = 100000.0f;
        float aspect_       = 1280.0f / 720.0f;

        ActiveView   active_view_{};
        CameraSource source_ = CameraSource::FreeFly;

        // Diagnostic (#219): whether the Scene source resolved its handle the last
        // time update_active_view() ran, so it logs only the transition edges (a
        // flip to/from "holding last view") rather than per frame.
        bool scene_source_resolved_ = false;
        bool prefer_scene_camera_   = false;

        // Active scene-camera SELECTION ANCHOR: active_camera_id_ is the stable
        // anchor recorded when a scene-setup behavior selects a camera;
        // active_camera_entity_ is its live polytree handle, re-seated only on a
        // behavior-scene rebuild (never per frame). Projection params are captured
        // at selection (a reload re-reads them). Recorded regardless of source, so
        // the editor can later switch the source to Scene to preview.
        wz::scene::AuthoredEntityId          active_camera_id_{};
        wz::scene::RuntimeEntityId           active_camera_entity_ =
            wz::scene::INVALID_RUNTIME_ENTITY;
        wz::engine::assets::SceneCameraAsset active_camera_params_{};

        bool editor_camera_dirty_ = false;
    };
}
