// src/math/camera.cpp

#include <math/camera.h>
#include <math/vec3.h>

namespace wz::math
{
    Mat4 look_at_dx(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        // Left-handed, z-forward (Direct3D convention).
        // z = forward, x = right, y = reorthogonalized up.
        const Vec3 z = normalize(target - eye);
        const Vec3 x = normalize(cross(up, z));
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
