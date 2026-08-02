#include "math/frustum.h"
#include "math/mat4.h"
#include "math/vec3.h"

using namespace wz::math;

namespace wz::math
{
    bool intersects_sphere(const Frustum& f, const Vec3& c, float r)
    {
        for (const auto& p : f.planes)
        {
            float d = dot(p.normal, c) + p.distance;

            if (d < -r)
                return false;
        }

        return true;
    }

    bool intersects_sphere(const Frustum& f, const Sphere& s)
    {
        return intersects_sphere(f, s.center, s.radius);
    }

    Frustum frustum_from_view_projection(const Mat4& m)
    {
        Frustum f;

        // LEFT  = col3 + col0
        f.planes[0].normal.x = m.m[3] + m.m[0];
        f.planes[0].normal.y = m.m[7] + m.m[4];
        f.planes[0].normal.z = m.m[11] + m.m[8];
        f.planes[0].distance = m.m[15] + m.m[12];

        // RIGHT = col3 - col0
        f.planes[1].normal.x = m.m[3] - m.m[0];
        f.planes[1].normal.y = m.m[7] - m.m[4];
        f.planes[1].normal.z = m.m[11] - m.m[8];
        f.planes[1].distance = m.m[15] - m.m[12];

        // BOTTOM = col3 + col1
        f.planes[2].normal.x = m.m[3] + m.m[1];
        f.planes[2].normal.y = m.m[7] + m.m[5];
        f.planes[2].normal.z = m.m[11] + m.m[9];
        f.planes[2].distance = m.m[15] + m.m[13];

        // TOP = col3 - col1
        f.planes[3].normal.x = m.m[3] - m.m[1];
        f.planes[3].normal.y = m.m[7] - m.m[5];
        f.planes[3].normal.z = m.m[11] - m.m[9];
        f.planes[3].distance = m.m[15] - m.m[13];

        // NEAR = col3 + col2
        f.planes[4].normal.x = m.m[3] + m.m[2];
        f.planes[4].normal.y = m.m[7] + m.m[6];
        f.planes[4].normal.z = m.m[11] + m.m[10];
        f.planes[4].distance = m.m[15] + m.m[14];

        // FAR = col3 - col2
        f.planes[5].normal.x = m.m[3] - m.m[2];
        f.planes[5].normal.y = m.m[7] - m.m[6];
        f.planes[5].normal.z = m.m[11] - m.m[10];
        f.planes[5].distance = m.m[15] - m.m[14];

        // normalize all planes
        for (auto& p : f.planes)
        {
            float len = length(p.normal);
            p.normal = p.normal / len;
            p.distance /= len;
        }

        return f;
    }

    bool contains_point(const Frustum& f, const Vec3& p)
    {
        for (const auto& plane : f.planes)
        {
            if (dot(plane.normal, p) + plane.distance < 0.0f)
                return false;
        }
        return true;
    }
}