#pragma once

#include "math_types.h"

namespace wz::math
{
    Frustum frustum_from_view_projection(const Mat4& vp);

    bool contains_point(const Frustum& f, const Vec3& p);

    bool intersects_sphere(const Frustum& f, const Sphere& s);

    bool intersects_sphere(const Frustum& f, const Vec3& center, float radius);
}