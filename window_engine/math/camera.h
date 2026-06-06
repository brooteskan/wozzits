#pragma once

#include "math_types.h"

namespace wz::math
{
    // Left-handed look-at view matrix (Direct3D / Metal depth [0,1] convention).
    // eye    — world-space camera position
    // target — world-space point the camera looks toward
    // up     — world-space up hint vector (typically {0,1,0})
    Mat4 look_at_dx(const Vec3& eye, const Vec3& target, const Vec3& up);

    // View matrix from a camera Transform (position + rotation quaternion).
    Mat4 view_matrix(const Transform& camera);

    // View matrix from a Camera (delegates to view_matrix(Transform)).
    Mat4 view_matrix(const Camera& camera);

    // Perspective projection from a Camera's stored parameters.
    Mat4 projection_perspective(const Camera& camera);
}