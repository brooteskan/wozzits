#include <math/quaternion.h>
#include <math/compare.h>


namespace wz::math
{

    bool near_equal(const float a, const  float b, const Eps eps)
    {
        float diff = std::abs(a - b);

        float scale = std::max(1.0f, std::max(std::abs(a), std::abs(b)));

        return diff <= eps.abs + eps.rel * scale;
    }

    bool near_equal(const float a, const float b)
    {
        return near_equal(a, b, DEFAULT_COMPARE_EPS);
    }

    bool near_equal(const Vec3& a, const Vec3& b, const Eps eps)
    {
        return near_equal(a.x, b.x, eps) &&
            near_equal(a.y, b.y, eps) &&
            near_equal(a.z, b.z, eps);
    }

    bool near_equal(const Vec3& a, const Vec3& b)
    {
        return near_equal(a, b, DEFAULT_COMPARE_EPS);
    }

    bool near_equal(const Quaternion& a, const Quaternion& b, const Eps eps)
    {
        float dt = std::abs(wz::math::dot(a, b));
        return dt >= 1.0f - eps.rel;
    }

    bool near_equal(const Quaternion& a, const Quaternion& b)
    {
        return near_equal(a, b, DEFAULT_COMPARE_EPS);
    }

    bool near_equal(const Mat4& a, const Mat4& b, const Eps eps)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (!near_equal(a.m[i], b.m[i], eps))
                return false;
        }
        return true;
    }

    bool near_equal(const Mat4& a, const Mat4& b)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (!near_equal(a.m[i], b.m[i], DEFAULT_COMPARE_EPS))
                return false;
        }
        return true;
    }

    bool near_equal(const Transform& a, const Transform& b, const Eps eps)
    {
        return near_equal(a.position, b.position, eps) &&
            near_equal(a.rotation, b.rotation, eps) &&
            near_equal(a.scale, b.scale, eps);
    }

    bool near_equal(const Transform& a, const Transform& b)
    {
        return near_equal(a.position, b.position, DEFAULT_COMPARE_EPS) &&
            near_equal(a.rotation, b.rotation, DEFAULT_COMPARE_EPS) &&
            near_equal(a.scale, b.scale, DEFAULT_COMPARE_EPS);
    }
}

