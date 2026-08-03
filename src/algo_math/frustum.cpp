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

        // Normalize all planes.
        //
        // A DEGENERATE view-projection gives a zero-length normal, and dividing
        // by it produced 0/0 = NaN in all four components. That is not a corner
        // case: ViewData::view_projection is declared `Mat4 view_projection{}`
        // (compiled_scene.h), i.e. value-initialised to all ZEROS rather than to
        // identity, so every plane here is col3 +/- colN = (0,0,0) on any scene
        // runtime built before the first camera update -- and it stays that way
        // for a scene with no active camera.
        //
        // The failure was invisible because both consumers test in the ACCEPT
        // direction: intersects_sphere's `if (d < -r) return false;` and
        // contains_point's `if (dot + distance < 0) return false;` are FALSE for a
        // NaN, so a NaN frustum passes everything and culling silently becomes a
        // no-op. Nothing disappears from the frame, which is why it looked fine.
        //
        // Leaving a degenerate plane un-normalised keeps `d` finite and the plane
        // inert, which is the right meaning for "this view has no such plane".
        // Written in the reject direction so a NaN length is caught too.
        for (auto& p : f.planes)
        {
            const float len = length(p.normal);
            if (!(len > 0.0f))
                continue;

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