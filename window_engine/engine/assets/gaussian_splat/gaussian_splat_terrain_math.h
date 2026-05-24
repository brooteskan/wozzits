#pragma once

// engine/assets/gaussian_splat/gaussian_splat_terrain_math.h
//
// Shared math primitives for terrain splat compilers.
//
// These live in a header (not a .cpp anonymous namespace) so that both
// the simple terrain-surface compiler and the multi-field recipe
// compiler can use the same tested math without duplication.
//
// All functions are inline to avoid ODR violations across TUs.

#include <engine/assets/scalar_field/scalar_field.h>

#include <cmath>
#include <cstdint>

namespace wz::engine::assets::terrain_math
{
    constexpr float kSH_C0 = 0.28209479177387814f;

    struct Vec3f
    {
        float x, y, z;
    };

    inline Vec3f operator+(Vec3f a, Vec3f b) noexcept
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    inline Vec3f operator-(Vec3f a, Vec3f b) noexcept
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    inline Vec3f operator*(Vec3f a, float s) noexcept
    {
        return { a.x * s, a.y * s, a.z * s };
    }

    inline Vec3f operator*(float s, Vec3f a) noexcept
    {
        return a * s;
    }

    inline float dot3(Vec3f a, Vec3f b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline Vec3f cross3(Vec3f a, Vec3f b) noexcept
    {
        return Vec3f{
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    inline float length3(Vec3f a) noexcept
    {
        return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    }

    inline Vec3f normalize3(Vec3f a) noexcept
    {
        const float len = length3(a);
        if (len <= 1e-12f)
            return Vec3f{ 0.0f, 1.0f, 0.0f };
        const float inv = 1.0f / len;
        return Vec3f{ a.x * inv, a.y * inv, a.z * inv };
    }

    // Linear interpolation between two vectors.
    inline Vec3f lerp3(Vec3f a, Vec3f b, float t) noexcept
    {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
        };
    }

    // ── Heightfield finite-difference derivatives ──────────────────────

    // Central-difference derivative of h(ix, iz) along the u (x) axis.
    // One-sided at boundaries.  Returns dh per cell step.
    inline float dh_du(
        const ScalarFieldData& field, uint32_t ix, uint32_t iz) noexcept
    {
        const uint32_t W = field.width;
        if (ix == 0)
            return field.at(1, iz) - field.at(0, iz);
        if (ix + 1 >= W)
            return field.at(W - 1, iz) - field.at(W - 2, iz);
        return 0.5f * (field.at(ix + 1, iz) - field.at(ix - 1, iz));
    }

    // Central-difference derivative of h(ix, iz) along the v (z) axis.
    inline float dh_dv(
        const ScalarFieldData& field, uint32_t ix, uint32_t iz) noexcept
    {
        const uint32_t H = field.height;
        if (iz == 0)
            return field.at(ix, 1) - field.at(ix, 0);
        if (iz + 1 >= H)
            return field.at(ix, H - 1) - field.at(ix, H - 2);
        return 0.5f * (field.at(ix, iz + 1) - field.at(ix, iz - 1));
    }

    // ── Quaternion from orthonormal frame ──────────────────────────────

    struct Quat
    {
        float w, x, y, z;
    };

    // Build a quaternion (w, x, y, z) that rotates the standard basis
    // (Xhat, Yhat, Zhat) to the given orthonormal frame (Xs, Ys, Zs).
    // Standard "rotation matrix -> quaternion" with sign-stable branches.
    inline Quat quat_from_frame(Vec3f Xs, Vec3f Ys, Vec3f Zs) noexcept
    {
        const float m00 = Xs.x, m01 = Ys.x, m02 = Zs.x;
        const float m10 = Xs.y, m11 = Ys.y, m12 = Zs.y;
        const float m20 = Xs.z, m21 = Ys.z, m22 = Zs.z;

        const float tr = m00 + m11 + m22;
        Quat q{};

        if (tr > 0.0f)
        {
            const float S = std::sqrt(tr + 1.0f) * 2.0f;
            q.w = 0.25f * S;
            q.x = (m21 - m12) / S;
            q.y = (m02 - m20) / S;
            q.z = (m10 - m01) / S;
        }
        else if (m00 > m11 && m00 > m22)
        {
            const float S = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            q.w = (m21 - m12) / S;
            q.x = 0.25f * S;
            q.y = (m01 + m10) / S;
            q.z = (m02 + m20) / S;
        }
        else if (m11 > m22)
        {
            const float S = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            q.w = (m02 - m20) / S;
            q.x = (m01 + m10) / S;
            q.y = 0.25f * S;
            q.z = (m12 + m21) / S;
        }
        else
        {
            const float S = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            q.w = (m10 - m01) / S;
            q.x = (m02 + m20) / S;
            q.y = (m12 + m21) / S;
            q.z = 0.25f * S;
        }

        const float len = std::sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
        if (len > 1e-12f)
        {
            const float inv = 1.0f / len;
            q.w *= inv; q.x *= inv; q.y *= inv; q.z *= inv;
        }
        else
        {
            q = Quat{ 1.0f, 0.0f, 0.0f, 0.0f };
        }
        return q;
    }

    // ── SH DC encoding helpers ────────────────────────────────────────

    inline float to_sh_dc(float display) noexcept
    {
        return (display - 0.5f) / kSH_C0;
    }

    inline float sh_dc_to_display(float sh_dc) noexcept
    {
        return sh_dc * kSH_C0 + 0.5f;
    }

}  // namespace wz::engine::assets::terrain_math
