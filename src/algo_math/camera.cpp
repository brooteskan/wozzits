// src/math/camera.cpp

#include <math/camera.h>
#include <math/vec3.h>

#include <cmath>

namespace wz::math
{
    namespace
    {
        // cross(up, forward) is the zero vector when the two are parallel, and
        // normalize() of that is zeros -- so looking straight up or straight down
        // with a world-up hint produced a rank-deficient basis whose x and z
        // columns were both (0,0,0) (#314, C1-C8). Measured: looking down from
        // (0,10,0) gave basis = [0 0 0 | 0 0 -1 | 0 0 0].
        //
        // Pick a different, guaranteed non-parallel hint instead of emitting a
        // degenerate matrix. Which axis is chosen is arbitrary -- at the poles
        // the roll about the view axis is genuinely undefined -- but it must be
        // a rotation, and it must be continuous with itself frame to frame.
        Vec3 usable_up(const Vec3& up, const Vec3& forward)
        {
            const Vec3 axis = cross(up, forward);
            const float len_sq = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
            if (len_sq > 1e-12f && std::isfinite(len_sq)) {
                return up;
            }
            // forward is (anti)parallel to up: fall back to the world axis that
            // is most perpendicular to it.
            return std::abs(forward.y) < std::abs(forward.z)
                ? Vec3{ 0.0f, 1.0f, 0.0f }
                : Vec3{ 0.0f, 0.0f, 1.0f };
        }
    }

    Mat4 look_at_dx(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        // Left-handed, z-forward (Direct3D convention).
        // z = forward, x = right, y = reorthogonalized up.
        const Vec3 z = normalize(target - eye);
        const Vec3 x = normalize(cross(usable_up(up, z), z));
        const Vec3 y = cross(z, x);

        // Build view matrix in column-major storage.
        // Row i = [basis_i | -dot(basis_i, eye)]
        Mat4 view = {};
        view.m[0]  = x.x;          view.m[1]  = y.x;          view.m[2]  = z.x;          view.m[3]  = 0.f;
        view.m[4]  = x.y;          view.m[5]  = y.y;          view.m[6]  = z.y;          view.m[7]  = 0.f;
        view.m[8]  = x.z;          view.m[9]  = y.z;          view.m[10] = z.z;          view.m[11] = 0.f;
        view.m[12] = -dot(x, eye); view.m[13] = -dot(y, eye); view.m[14] = -dot(z, eye); view.m[15] = 1.f;
        return view;
    }
}
