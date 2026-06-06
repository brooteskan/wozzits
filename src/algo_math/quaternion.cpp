#include <math/quaternion.h>
#include <math/vec3.h>
#include <algorithm>
#include <cmath>

namespace wz::math
{
    Quaternion quat_identity()
    {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }

    Quaternion normalize(const Quaternion& q)
    {
        float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (len == 0.0f)
            return Quaternion::identity();

        float inv = 1.0f / len;

        return {
            q.x * inv,
            q.y * inv,
            q.z * inv,
            q.w * inv
        };
    }

    Quaternion mul(const Quaternion& a, const Quaternion& b)
    {
        // Hamilton product
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        };
    }

    Quaternion from_axis_angle(const Vec3& axis, float angle)
    {
        Vec3 n = normalize(axis);  // uses Vec3 normalize

        float half = angle * 0.5f;
        float s = std::sin(half);
        float c = std::cos(half);

        return {
            n.x * s,
            n.y * s,
            n.z * s,
            c
        };
    }

    Quaternion from_rotation_matrix(const Mat4& m)
    {
        const float m00 = m.m[0];
        const float m01 = m.m[4];
        const float m02 = m.m[8];
        const float m10 = m.m[1];
        const float m11 = m.m[5];
        const float m12 = m.m[9];
        const float m20 = m.m[2];
        const float m21 = m.m[6];
        const float m22 = m.m[10];

        Quaternion q{};
        const float trace = m00 + m11 + m22;
        if (trace > 0.0f) {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            if (s <= 0.0f) {
                return Quaternion::identity();
            }
            q.w = 0.25f * s;
            q.x = (m21 - m12) / s;
            q.y = (m02 - m20) / s;
            q.z = (m10 - m01) / s;
        }
        else if (m00 > m11 && m00 > m22) {
            const float s = std::sqrt(std::max(
                0.0f,
                1.0f + m00 - m11 - m22)) * 2.0f;
            if (s <= 0.0f) {
                return Quaternion::identity();
            }
            q.w = (m21 - m12) / s;
            q.x = 0.25f * s;
            q.y = (m01 + m10) / s;
            q.z = (m02 + m20) / s;
        }
        else if (m11 > m22) {
            const float s = std::sqrt(std::max(
                0.0f,
                1.0f + m11 - m00 - m22)) * 2.0f;
            if (s <= 0.0f) {
                return Quaternion::identity();
            }
            q.w = (m02 - m20) / s;
            q.x = (m01 + m10) / s;
            q.y = 0.25f * s;
            q.z = (m12 + m21) / s;
        }
        else {
            const float s = std::sqrt(std::max(
                0.0f,
                1.0f + m22 - m00 - m11)) * 2.0f;
            if (s <= 0.0f) {
                return Quaternion::identity();
            }
            q.w = (m10 - m01) / s;
            q.x = (m02 + m20) / s;
            q.y = (m12 + m21) / s;
            q.z = 0.25f * s;
        }

        return normalize(q);
    }

    Vec3 rotate(const Quaternion& q, const Vec3& v)
    {
        // Convert vector to quaternion (w = 0)
        Quaternion p{ v.x, v.y, v.z, 0.0f };

        // q * p * q^-1
        Quaternion qn = normalize(q);

        Quaternion q_conj{
            -qn.x,
            -qn.y,
            -qn.z,
             qn.w
        };

        Quaternion r = mul(mul(qn, p), q_conj);

        return { r.x, r.y, r.z };
    }

    Vec4 vec4_mul_point(const Mat4& m, const Vec3& v)
    {
        return {
            m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12],
            m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
            m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14],
            m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15]
        };
    }

    float dot(const Quaternion& a, const Quaternion& b)
    {
        return a.x * b.x +
            a.y * b.y +
            a.z * b.z +
            a.w * b.w;
    }

    // ── Axis extraction ──────────────────────────────────────────────
    //
    // Rotation matrix columns from a unit quaternion {x, y, z, w}:
    //
    //   col0 (X axis) = [ 1 - 2(y²+z²),  2(xy+wz),    2(xz-wy)   ]
    //   col1 (Y axis) = [ 2(xy-wz),      1 - 2(x²+z²), 2(yz+wx)  ]
    //   col2 (Z axis) = [ 2(xz+wy),      2(yz-wx),    1 - 2(x²+y²)]
    //
    // These assume a unit quaternion.  No normalization is performed —
    // the caller should normalize beforehand if needed.

    Vec3 quat_axis_x(const Quaternion& q)
    {
        return {
            1.0f - 2.0f * (q.y * q.y + q.z * q.z),
            2.0f * (q.x * q.y + q.w * q.z),
            2.0f * (q.x * q.z - q.w * q.y)
        };
    }

    Vec3 quat_axis_y(const Quaternion& q)
    {
        return {
            2.0f * (q.x * q.y - q.w * q.z),
            1.0f - 2.0f * (q.x * q.x + q.z * q.z),
            2.0f * (q.y * q.z + q.w * q.x)
        };
    }

    Vec3 quat_axis_z(const Quaternion& q)
    {
        return {
            2.0f * (q.x * q.z + q.w * q.y),
            2.0f * (q.y * q.z - q.w * q.x),
            1.0f - 2.0f * (q.x * q.x + q.y * q.y)
        };
    }
}
