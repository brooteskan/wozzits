#include <math/mat4.h>
#include <math/quaternion.h>
#include <math/camera.h>
#include <math/projection.h>
#include <math/vec3.h>
#include <cmath>
#include <algorithm>

namespace wz::math
{
    Mat4 projection_perspective(float fov_y, float aspect, float near_z, float far_z)
    {
        float f = 1.0f / std::tan(fov_y * 0.5f);

        Mat4 m = {};

        m.m[0] = f / aspect;
        m.m[5] = f;
        m.m[10] = (far_z + near_z) / (near_z - far_z);
        m.m[11] = -1.0f;
        m.m[14] = (2.0f * far_z * near_z) / (near_z - far_z);

        return m;
    }

    Mat4 projection_perspective(const Camera& camera)
    {
        float f = 1.0f / std::tan(camera.fov_y * 0.5f);

        Mat4 m = {};

        m.m[0] = f / camera.aspect;
        m.m[5] = f;

        m.m[10] = (camera.far_plane + camera.near_plane) /
            (camera.near_plane - camera.far_plane);

        m.m[11] = -1.0f;

        m.m[14] = (2.0f * camera.far_plane * camera.near_plane) /
            (camera.near_plane - camera.far_plane);

        return m;
    }

    Mat4 projection_perspective_dx(float fov_y, float aspect, float near_z, float far_z)
    {
        const float f = 1.0f / std::tan(fov_y * 0.5f);

        Mat4 m = {};

        m.m[0]  = f / aspect;
        m.m[5]  = f;
        m.m[10] = far_z / (far_z - near_z);
        m.m[11] = 1.0f;
        m.m[14] = (-near_z * far_z) / (far_z - near_z);

        return m;
    }

    Mat4 projection_orthographic_dx(
        float width,
        float height,
        float near_z,
        float far_z)
    {
        Mat4 m = {};

        m.m[0] = 2.0f / width;
        m.m[5] = 2.0f / height;
        m.m[10] = 1.0f / (far_z - near_z);
        m.m[14] = -near_z / (far_z - near_z);
        m.m[15] = 1.0f;

        return m;
    }

    Mat4 view_matrix(const Transform& camera)
    {
        Quaternion qinv = {
            -camera.rotation.x,
            -camera.rotation.y,
            -camera.rotation.z,
             camera.rotation.w
        };

        Vec3 neg_pos = camera.position * -1.0f;

        Mat4 R = rotation(qinv);
        Mat4 T = translation(rotate(qinv, neg_pos));

        return mul(R, T);
    }

    Mat4 view_matrix(const Camera& camera)
    {
        const Transform& t = camera.transform;

        Quaternion qinv = {
            -t.rotation.x,
            -t.rotation.y,
            -t.rotation.z,
             t.rotation.w
        };

        Vec3 neg_pos = t.position * -1.0f;

        Mat4 R = rotation(qinv);
        Mat4 T = translation(rotate(qinv, neg_pos));

        return mul(R, T);
    }


    Mat4 mat4_identity()
    {
        return Mat4::identity();
    }

    Mat4 translation(const Vec3& t)
    {
        Mat4 r = Mat4::identity();

        // last column (column-major)
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;

        return r;
    }

    Mat4 scale(const Vec3& s)
    {
        Mat4 r = {};

        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        r.m[15] = 1.0f;

        return r;
    }

    Mat4 mul(const Mat4& a, const Mat4& b)
    {
        Mat4 r = {};

        // Column-major multiplication: r = a * b
        // r(col, row) = sum_k a(k, row) * b(col, k)

        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                r.m[col * 4 + row] =
                    a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                    a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                    a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                    a.m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }
        return r;
    }

    Vec3 mul_point(const Mat4& m, const Vec3& v)
    {
        // w = 1
        return {
            m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12],
            m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
            m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14]
        };
    }

    Vec3 mul_vector(const Mat4& m, const Vec3& v)
    {
        // w = 0 (no translation)
        return {
            m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
            m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
            m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z
        };
    }

    Mat4 rotation(const Quaternion& q)
    {
        Quaternion n = normalize(q);

        float x = n.x;
        float y = n.y;
        float z = n.z;
        float w = n.w;

        float xx = x * x;
        float yy = y * y;
        float zz = z * z;

        float xy = x * y;
        float xz = x * z;
        float yz = y * z;

        float wx = w * x;
        float wy = w * y;
        float wz = w * z;

        Mat4 m = {};

        // column-major layout
        m.m[0] = 1.0f - 2.0f * (yy + zz);
        m.m[1] = 2.0f * (xy + wz);
        m.m[2] = 2.0f * (xz - wy);
        m.m[3] = 0.0f;

        m.m[4] = 2.0f * (xy - wz);
        m.m[5] = 1.0f - 2.0f * (xx + zz);
        m.m[6] = 2.0f * (yz + wx);
        m.m[7] = 0.0f;

        m.m[8] = 2.0f * (xz + wy);
        m.m[9] = 2.0f * (yz - wx);
        m.m[10] = 1.0f - 2.0f * (xx + yy);
        m.m[11] = 0.0f;

        m.m[12] = 0.0f;
        m.m[13] = 0.0f;
        m.m[14] = 0.0f;
        m.m[15] = 1.0f;

        return m;
    }

    Mat4 transform(const Transform& t)
    {
        Mat4 S = scale(t.scale);
        Mat4 R = rotation(t.rotation);
        Mat4 T = translation(t.position);

        // apply S → R → T
        return mul(T, mul(R, S));
    }

    bool decompose_trs(
        const Mat4& m,
        Transform& out,
        float epsilon)
    {
        if (epsilon <= 0.0f) {
            epsilon = 1e-6f;
        }

        // Every gate below is a comparison, and every comparison against NaN is
        // FALSE -- which is the accept branch in all five of them. So the
        // strictest decomposition in the engine used to return true for an
        // all-NaN matrix and hand back a NaN scale (#314, C1-C1). That matters
        // because twelve call sites are written as
        //     if (!decompose_trs(...)) { keep the authored value; return; }
        // and depend on this refusing a matrix that must not be used -- among
        // them the save path (wozzits_app_v1.cpp authored_transform_from_local),
        // where a NaN reaches AuthoredTransform, is written to scene.json as
        // `null`, and is then refused by the reader on the next load.
        //
        // Check finiteness ONCE, up front, over the whole matrix: it is the only
        // form that cannot be written in the admitting direction by accident.
        for (const float value : m.m) {
            if (!std::isfinite(value)) {
                return false;
            }
        }

        if (std::abs(m.m[3]) > epsilon
            || std::abs(m.m[7]) > epsilon
            || std::abs(m.m[11]) > epsilon
            || std::abs(m.m[15] - 1.0f) > epsilon)
        {
            return false;
        }

        const Vec3 x{ m.m[0], m.m[1], m.m[2] };
        const Vec3 y{ m.m[4], m.m[5], m.m[6] };
        const Vec3 z{ m.m[8], m.m[9], m.m[10] };

        const float sx = length(x);
        const float sy = length(y);
        const float sz = length(z);
        if (sx <= epsilon || sy <= epsilon || sz <= epsilon) {
            return false;
        }

        const Vec3 rx = x / sx;
        const Vec3 ry = y / sy;
        const Vec3 rz = z / sz;

        if (std::abs(dot(rx, ry)) > epsilon
            || std::abs(dot(rx, rz)) > epsilon
            || std::abs(dot(ry, rz)) > epsilon)
        {
            return false;
        }

        const float determinant = dot(rx, cross(ry, rz));
        if (determinant <= epsilon) {
            return false;
        }
        const float determinant_epsilon = std::max(epsilon, 1e-4f);
        if (std::abs(determinant - 1.0f) > determinant_epsilon) {
            return false;
        }

        Mat4 rotation_matrix = Mat4::identity();
        rotation_matrix.m[0] = rx.x;
        rotation_matrix.m[1] = rx.y;
        rotation_matrix.m[2] = rx.z;
        rotation_matrix.m[4] = ry.x;
        rotation_matrix.m[5] = ry.y;
        rotation_matrix.m[6] = ry.z;
        rotation_matrix.m[8] = rz.x;
        rotation_matrix.m[9] = rz.y;
        rotation_matrix.m[10] = rz.z;

        out.position = { m.m[12], m.m[13], m.m[14] };
        out.rotation = from_rotation_matrix(rotation_matrix);
        out.scale = { sx, sy, sz };
        return true;
    }

    Transform rigid_pose_from_matrix(const Mat4& m)
    {
        Transform out{};
        out.position = { m.m[12], m.m[13], m.m[14] };
        out.scale = { 1.0f, 1.0f, 1.0f };

        Vec3 x{ m.m[0], m.m[1], m.m[2] };
        Vec3 y{ m.m[4], m.m[5], m.m[6] };
        Vec3 z{ m.m[8], m.m[9], m.m[10] };

        const float sx = length(x);
        const float sy = length(y);
        const float sz = length(z);

        // A degenerate basis column carries no usable orientation; fall back to
        // identity rotation rather than dividing by ~0.
        constexpr float kMinAxis = 1e-6f;
        if (sx <= kMinAxis || sy <= kMinAxis || sz <= kMinAxis) {
            out.rotation = Quaternion::identity();
            return out;
        }

        x = x / sx;
        y = y / sy;
        z = z / sz;

        // Normalized basis columns -> rotation matrix -> quaternion. No
        // orthogonality/determinant gate: from_rotation_matrix stays stable for
        // a near-orthonormal basis, which is exactly the case decompose_trs
        // rejects.
        Mat4 rotation_matrix = Mat4::identity();
        rotation_matrix.m[0] = x.x;
        rotation_matrix.m[1] = x.y;
        rotation_matrix.m[2] = x.z;
        rotation_matrix.m[4] = y.x;
        rotation_matrix.m[5] = y.y;
        rotation_matrix.m[6] = y.z;
        rotation_matrix.m[8] = z.x;
        rotation_matrix.m[9] = z.y;
        rotation_matrix.m[10] = z.z;

        out.rotation = from_rotation_matrix(rotation_matrix);
        return out;
    }

    float max_scale(const Mat4& m)
    {
        // assumes column-major basis vectors
        Vec3 x = { m.m[0], m.m[1], m.m[2] };
        Vec3 y = { m.m[4], m.m[5], m.m[6] };
        Vec3 z = { m.m[8], m.m[9], m.m[10] };

        float sx = length(x);
        float sy = length(y);
        float sz = length(z);

        return std::max(sx, std::max(sy, sz));
    }

    bool intersects(const Frustum& f, const Sphere& s)
    {
        for (int i = 0; i < 6; ++i)
        {
            const Vec4& p = f.planes[i].asVec4;

            float distance =
                p.x * s.center.x +
                p.y * s.center.y +
                p.z * s.center.z +
                p.w;

            if (distance < -s.radius)
                return false;
        }

        return true;
    }
}
