#pragma once

#include <math/math_types.h>

namespace wz::math
{   
    [[deprecated("Use Quaternion::identity() instead")]]
    Quaternion quat_identity();

    Quaternion normalize(const Quaternion& q);

    Quaternion mul(const Quaternion& a, const Quaternion& b);

    Quaternion from_axis_angle(const Vec3& axis, float angle);
    // Extracts a quaternion from the upper-left 3x3 rotation matrix.
    // The input is expected to be orthonormal; use decompose_trs() for full
    // TRS matrices that may include translation and scale.
    Quaternion from_rotation_matrix(const Mat4& m);

    Vec3 rotate(const Quaternion& q, const Vec3& v);

    Vec4 vec4_mul_point(const Mat4& m, const Vec3& v);

    Mat4 to_mat4(const Quaternion& q);

    float dot(const Quaternion& a, const Quaternion& b);

    // Extract the columns (axes) of the 3×3 rotation matrix encoded by a
    // unit quaternion.  These are the world-space directions of the local
    // X, Y, Z axes after rotation.
    //
    // Convention: Quaternion stores {x, y, z, w}.
    //
    // Equivalent to building the full rotation matrix via to_mat4() and
    // reading columns 0, 1, 2, but avoids the 16-element intermediate.
    Vec3 quat_axis_x(const Quaternion& q);
    Vec3 quat_axis_y(const Quaternion& q);
    Vec3 quat_axis_z(const Quaternion& q);
}
