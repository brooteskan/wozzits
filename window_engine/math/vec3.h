#pragma once
#include <math/math_types.h>
#include <cmath>


namespace wz::math
{
    Vec3 operator+(const Vec3& a, const Vec3& b);
    Vec3 operator-(const Vec3& a, const Vec3& b);
    Vec3 operator*(const Vec3& v, float s);
    Vec3 operator/(const Vec3& v, float s);

    float dot(const Vec3& a, const Vec3& b);
    Vec3 cross(const Vec3& a, const Vec3& b);

    float length(const Vec3& v);
    Vec3 normalize(const Vec3& v);
}
