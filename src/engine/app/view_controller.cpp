// src/engine/app/view_controller.cpp

#include <engine/app/view_controller.h>

#include <bench/flying_camera.h>
#include <math/mat4.h>
#include <math/math_types.h>
#include <math/projection.h>

#include <string>

namespace wz::app
{
    bool ViewController::update_free_fly(
        const wz::input::InputState& input, float dt)
    {
        const wz::bench::FlyingCamera before = camera_;
        wz::bench::update_flying_camera(camera_, input, dt);

        // Compare pose so a hold-still frame stays clean (does not dirty the
        // editor camera). Any component change counts as movement.
        const bool moved =
            camera_.x != before.x || camera_.y != before.y
            || camera_.z != before.z
            || camera_.orientation.x != before.orientation.x
            || camera_.orientation.y != before.orientation.y
            || camera_.orientation.z != before.orientation.z
            || camera_.orientation.w != before.orientation.w;

        // Editor viewport only: a moved free-fly camera is unsaved viewport state
        // (standalone play does not author the editor camera).
        if (moved && !prefer_scene_camera_) {
            editor_camera_dirty_ = true;
        }
        return moved;
    }

    void ViewController::select_scene_camera(
        const wz::scene::AuthoredEntityId& id,
        wz::scene::RuntimeEntityId entity,
        const wz::engine::assets::SceneCameraAsset& params,
        wz::Logger& logger)
    {
        active_camera_id_     = id;
        active_camera_entity_ = entity;
        active_camera_params_ = params;

        // Play hosts (standalone) flip the active source to the scene camera; the
        // editor records the anchor but stays on the free-fly edit camera so you
        // can navigate (a later editor toggle can switch the source to Scene).
        if (prefer_scene_camera_) {
            source_ = CameraSource::Scene;
        }
        logger.info(
            "scene_camera: active camera selected on node '" + id
            + "' (source="
            + (source_ == CameraSource::Scene ? "scene" : "free-fly")
            + ")");
    }

    void ViewController::clear_scene_camera()
    {
        // Re-decide the active camera on every (re)load: drop the prior anchor and
        // fall back to the free-fly source until a scene-setup behavior selects a
        // camera (and, in play, prefer_scene_camera_ flips the source).
        active_camera_id_.clear();
        active_camera_entity_ = wz::scene::INVALID_RUNTIME_ENTITY;
        source_ = CameraSource::FreeFly;
    }

    void ViewController::update_active_view(
        const std::optional<wz::math::Mat4>& scene_camera_world,
        std::size_t node_count,
        bool scene_live,
        wz::Logger& logger)
    {
        if (source_ == CameraSource::Scene) {
            // The host resolves the selected camera node's live world transform
            // and passes it in. If it could not be resolved this frame (e.g.
            // mid-rebuild), keep the previous active view rather than dropping to
            // free-fly — so a transient invalid handle cannot flip the camera.
            const bool resolved = scene_camera_world.has_value();

            // DIAGNOSTIC (#219): log only the resolve/unresolve EDGES, so a flip
            // shows up as a single line instead of per-frame spam.
            if (resolved != scene_source_resolved_) {
                logger.warn(
                    std::string("scene camera source ")
                    + (resolved ? "RESOLVED" : "UNRESOLVED (holding last view)")
                    + " entity=" + std::to_string(active_camera_entity_)
                    + " node_count=" + std::to_string(node_count)
                    + " behavior_scene=" + (scene_live ? "live" : "null"));
                scene_source_resolved_ = resolved;
            }

            if (resolved) {
                const wz::math::Mat4& world = *scene_camera_world;

                // Extract the camera node's rigid pose from its world matrix
                // robustly. decompose_trs is intentionally NOT used here — its
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
}
