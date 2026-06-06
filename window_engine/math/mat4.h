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
}
