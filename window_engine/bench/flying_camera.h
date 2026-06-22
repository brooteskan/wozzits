#pragma once
// bench/flying_camera.h

#include <input/input.h>
#include <math/math_types.h>

namespace wz::bench
{
    struct FlyingCamera
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        wz::math::Quaternion orientation = wz::math::Quaternion::identity();

        float move_speed       = 5.0f;
        float look_speed       = 0.0005f;
        float boost_multiplier = 3.0f;

        // Z/C roll rate in radians/second.  Independent of move_speed —
        // a fly camera over a 1000-unit terrain should not roll faster
        // than one over a 1-unit scene.
        float roll_speed       = 1.5f;
    };

    struct CameraBasis
    {
        wz::math::Vec3 right;
        wz::math::Vec3 up;
        wz::math::Vec3 forward;
    };

    CameraBasis camera_basis(const FlyingCamera& cam);

    // Left-handed (DX) view matrix for the camera's current pose: the 3x3 rows
    // are the camera basis (right/up/forward), translation is -(basis . pos),
    // so mul_point(V, p) = (dot(p-pos, right), dot(p-pos, up), dot(p-pos, fwd)).
    wz::math::Mat4 view_matrix(const FlyingCamera& cam);

    void update_flying_camera(
        FlyingCamera&                cam,
        const wz::input::InputState& input,
        float                        dt);
}
