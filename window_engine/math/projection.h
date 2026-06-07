#pragma once

#include "math_types.h"

namespace wz::math
{
    // Right-handed, depth [-1, 1] (OpenGL / Vulkan convention)
    Mat4 projection_perspective(float fov_y_radians, float aspect, float near_z, float far_z);

    // Left-handed, depth [0, 1] (Direct3D / Metal convention)
    Mat4 projection_perspective_dx(float fov_y_radians, float aspect, float near_z, float far_z);

    // Left-handed orthographic, depth [0, 1] (Direct3D / Metal convention)
    Mat4 projection_orthographic_dx(float width, float height, float near_z, float far_z);

    bool intersects(const Frustum& f, const Sphere& s);
}
