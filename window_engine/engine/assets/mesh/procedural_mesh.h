#pragma once
// window_engine/engine/assets/mesh/procedural_mesh.h

#include <engine/assets/mesh/mesh.h>

namespace wz::engine::assets
{
    [[nodiscard]] MeshData make_triangle_mesh();
    [[nodiscard]] MeshData make_quad_mesh();
    [[nodiscard]] MeshData make_cube_mesh();

    // Icosphere: a regular icosahedron (20 faces) whose triangles are each
    // subdivided `subdivisions` times, with every vertex projected onto the
    // sphere of the given radius. Deterministic (no RNG). Triangle count is
    // 20 * 4^subdivisions; normals are the normalized positions.
    [[nodiscard]] MeshData make_sphere_mesh(
        uint32_t subdivisions = 2, float radius = 1.0f);
}