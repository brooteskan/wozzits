#pragma once
#include <math/math_types.h>

namespace wz::math
{   
    [[deprecated("Use Mat4::identity() instead")]]
    Mat4 mat4_identity();

    Mat4 translation(const Vec3& t);
    Mat4 scale(const Vec3& s);

    Mat4 mul(const Mat4& a, const Mat4& b);

    Vec3 mul_point(const Mat4& m, const Vec3& v);
    Vec3 mul_vector(const Mat4& m, const Vec3& v);

    Mat4 rotation(const Quaternion& q);
    Mat4 transform(const Transform& t);

    bool decompose_trs(
        const Mat4& m,
        Transform& out,
        float epsilon = 1e-6f);

    // Extract a rigid pose (translation + rotation; scale discarded) from an
    // affine matrix WITHOUT decompose_trs's strict validity gates. Normalizes
    // the basis columns, so a uniformly-scaled and/or slightly non-orthonormal
    // matrix -- e.g. floating-point drift accumulated through chained rotations
    // -- still yields a stable rotation instead of being rejected. Intended for
    // deriving a camera's view from its live world transform, where a hard
    // rejection (as decompose_trs does) would leave an identity pose and snap
    // the camera to the origin. The returned scale is always 1; rotation is
    // identity only if a basis column is degenerate (near-zero length).
    Transform rigid_pose_from_matrix(const Mat4& m);
}
