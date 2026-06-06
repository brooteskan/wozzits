#pragma once

#include <include/math/math_types.h>
#include <include/math/mat4.h>

namespace wz::math
{
    /**
     * @brief Constructs an identity transform.
     *
     * Identity means:
     * - position = (0,0,0)
     * - rotation = identity quaternion
     * - scale    = (1,1,1)
     */
    Transform identity();

    /**
     * @brief Converts an SRT transform into a 4x4 matrix.
     *
     * The resulting matrix applies:
     * 1. Scale
     * 2. Rotation
     * 3. Translation
     */
    Mat4 mat4(const Transform& t);

    /**
     * @brief Composes two transforms.
     *
     * Result represents applying `a` followed by `b` in local space.
     *
     * NOTE: This is not the same as matrix multiplication unless explicitly defined
     * that way in implementation.
     */
    Transform mul(const Transform& a, const Transform& b);

    /**
     * @brief Creates a translation-only transform.
     *
     * Rotation = identity
     * Scale = (1,1,1)
     */
    Transform translation(float x, float y, float z);

    /**
     * @brief Creates a uniform or non-uniform scale transform.
     *
     * Rotation = identity
     * Position = (0,0,0)
     */
    Transform scale(float x, float y, float z);

    /**
     * @brief Creates a rotation-only transform from a quaternion.
     *
     * Position = (0,0,0)
     * Scale = (1,1,1)
     */
    Transform rotation(const Quaternion& q);

    /**
     * @brief Inverts a transform.
     *
     * Produces a transform that reverses the effect of t.
     */
    Transform inverse(const Transform& t);

    /**
     * @brief Transforms a point using a matrix.
     */
    Vec3 mul_point(const Mat4& m, const Vec3& p);

    /**
     * @brief Transforms a point using a matrix.
     */
    float max_scale(const Mat4& m);

    Sphere transform_sphere(const Mat4& m, const Sphere& b)
    {
        Sphere out;
        out.center = mul_point(m, b.center);
        out.radius = b.radius * max_scale(m);
        return out;
    }
}