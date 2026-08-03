#pragma once
#include <algorithm>
#include <math/math_types.h>

namespace wz::math
{
    struct Eps
    {
        const float abs = 1e-6f;   // absolute tolerance
        const float rel = 1e-5f;   // relative tolerance
    };

     constexpr Eps DEFAULT_COMPARE_EPS{};

     bool near_equal(const float, const float, const Eps);
     bool near_equal(const float, const float);
     bool near_equal(const Vec3&, const Vec3&, const Eps);
     bool near_equal(const Vec3&, const Vec3&);
     bool near_equal(const Quaternion&, const Quaternion&, const Eps);
     bool near_equal(const Quaternion&, const Quaternion&);
     bool near_equal(const Mat4&, const Mat4&, const Eps);
    bool near_equal(const Mat4&, const Mat4&);
    bool near_equal(const Transform&, const Transform&, const Eps);
    bool near_equal(const Transform&, const Transform&);
}