// src/engine/flying_camera.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <engine/flying_camera.h>
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

        const CameraBasis b = camera_basis(cam);

        // Mouse look: incremental rotations around the camera's own axes.
        // dx → rotate around local up (yaw), dy → rotate around local right (pitch).
        const float dx = static_cast<float>(input.mouse.dx) * cam.look_speed;
        const float dy = static_cast<float>(input.mouse.dy) * cam.look_speed;

        if (dx != 0.0f)
            cam.orientation = mul(from_axis_angle(b.up, dx), cam.orientation);
        if (dy != 0.0f)
            cam.orientation = mul(from_axis_angle(b.right, dy), cam.orientation);

        // Z/C: roll around forward axis.
        const float roll_speed = cam.move_speed * 0.5f * dt;
        if (input.keyboard.down['Z'])
            cam.orientation = mul(from_axis_angle(b.forward, -roll_speed), cam.orientation);
        if (input.keyboard.down['C'])
            cam.orientation = mul(from_axis_angle(b.forward,  roll_speed), cam.orientation);

        cam.orientation = normalize(cam.orientation);

        // Movement along camera-local axes.
        const float speed = cam.move_speed * dt
            * (input.keyboard.down[VK_SHIFT] ? cam.boost_multiplier : 1.0f);

        if (input.keyboard.down['W']) { cam.x += b.forward.x * speed; cam.y += b.forward.y * speed; cam.z += b.forward.z * speed; }
        if (input.keyboard.down['S']) { cam.x -= b.forward.x * speed; cam.y -= b.forward.y * speed; cam.z -= b.forward.z * speed; }
        if (input.keyboard.down['A']) { cam.x -= b.right.x   * speed; cam.y -= b.right.y   * speed; cam.z -= b.right.z   * speed; }
        if (input.keyboard.down['D']) { cam.x += b.right.x   * speed; cam.y += b.right.y   * speed; cam.z += b.right.z   * speed; }
        if (input.keyboard.down['E']) { cam.x += b.up.x      * speed; cam.y += b.up.y      * speed; cam.z += b.up.z      * speed; }
        if (input.keyboard.down['Q']) { cam.x -= b.up.x      * speed; cam.y -= b.up.y      * speed; cam.z -= b.up.z      * speed; }
    }
}
