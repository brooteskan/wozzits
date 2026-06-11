// src/bench/flying_camera.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <bench/flying_camera.h>
#include <math/quaternion.h>

namespace wz::bench
{
    CameraBasis camera_basis(const FlyingCamera& cam)
    {
        using namespace wz::math;
        return {
            rotate(cam.orientation, Vec3{ 1.0f, 0.0f, 0.0f }),
            rotate(cam.orientation, Vec3{ 0.0f, 1.0f, 0.0f }),
            rotate(cam.orientation, Vec3{ 0.0f, 0.0f, 1.0f }),
        };
    }

    void update_flying_camera(
        FlyingCamera&                cam,
        const wz::input::InputState& input,
        float                        dt)
    {
        using namespace wz::math;

        // Mouse look: yaw around WORLD up, pitch around LOCAL right.
        //
        // Using local up for yaw produces unwanted roll accumulation when
        // the camera has any non-zero pitch — circular mouse motion ends up
        // rolling the view because the local up axis has a forward component
        // and the successive yaw/pitch compositions don't commute.  Yawing
        // around world up keeps roll an explicit user action (Z/C only).
        const float dx = static_cast<float>(input.mouse.dx) * cam.look_speed;
        const float dy = static_cast<float>(input.mouse.dy) * cam.look_speed;

        const Vec3 world_up{ 0.0f, 1.0f, 0.0f };

        if (dx != 0.0f)
            cam.orientation = mul(from_axis_angle(world_up, dx), cam.orientation);
        if (dy != 0.0f) {
            const CameraBasis b_yaw = camera_basis(cam);
            cam.orientation = mul(from_axis_angle(b_yaw.right, dy), cam.orientation);
        }

        // Z/C: roll around forward axis at a fixed angular rate (rad/sec),
        // independent of move_speed.  Recompute basis after the mouse-look
        // step above so roll happens around the post-yaw/pitch forward.
        const CameraBasis b_post = camera_basis(cam);
        const float roll_step = cam.roll_speed * dt;
        if (input.keyboard.down['Z'])
            cam.orientation = mul(from_axis_angle(b_post.forward, -roll_step), cam.orientation);
        if (input.keyboard.down['C'])
            cam.orientation = mul(from_axis_angle(b_post.forward,  roll_step), cam.orientation);

        cam.orientation = normalize(cam.orientation);

        // Movement along the final camera-local axes for this frame.
        const CameraBasis b_move = camera_basis(cam);
        const float speed = cam.move_speed * dt
            * (input.keyboard.down[VK_SHIFT] ? cam.boost_multiplier : 1.0f);

        if (input.keyboard.down['W']) { cam.x += b_move.forward.x * speed; cam.y += b_move.forward.y * speed; cam.z += b_move.forward.z * speed; }
        if (input.keyboard.down['S']) { cam.x -= b_move.forward.x * speed; cam.y -= b_move.forward.y * speed; cam.z -= b_move.forward.z * speed; }
        if (input.keyboard.down['A']) { cam.x -= b_move.right.x   * speed; cam.y -= b_move.right.y   * speed; cam.z -= b_move.right.z   * speed; }
        if (input.keyboard.down['D']) { cam.x += b_move.right.x   * speed; cam.y += b_move.right.y   * speed; cam.z += b_move.right.z   * speed; }
        if (input.keyboard.down['E']) { cam.x += b_move.up.x      * speed; cam.y += b_move.up.y      * speed; cam.z += b_move.up.z      * speed; }
        if (input.keyboard.down['Q']) { cam.x -= b_move.up.x      * speed; cam.y -= b_move.up.y      * speed; cam.z -= b_move.up.z      * speed; }
    }
}
