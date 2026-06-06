#include <math/vec3.h>
#include <cmath>

namespace wz::math
{
    Vec3 operator+(const Vec3& a, const Vec3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    Vec3 operator-(const Vec3& a, const Vec3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    Vec3 operator*(const Vec3& v, float s)
    {
        return { v.x * s, v.y * s, v.z * s };
    }

    Vec3 operator*(float s, const Vec3& v)
    {
        return v * s;
    }

    Vec3 operator/(const Vec3& v, float s)
    {
        return { v.x / s, v.y / s, v.z / s };
    }

    float dot(const Vec3& a, const Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 cross(const Vec3& a, const Vec3& b)
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    float length(const Vec3& v)
    {
        return std::sqrt(dot(v, v));
    }

    Vec3 normalize(const Vec3& v)
    {
        float len = length(v);
        if (len == 0.0f)
            return { 0, 0, 0 };

        return v / len;
    }
}