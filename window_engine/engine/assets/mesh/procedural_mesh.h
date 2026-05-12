#pragma once
// window_engine/engine/assets/mesh/procedural_mesh.h

#include <engine/assets/mesh/mesh.h>

namespace wz::engine::assets
{
    [[nodiscard]] MeshData make_triangle_mesh();
    [[nodiscard]] MeshData make_quad_mesh();
    [[nodiscard]] MeshData make_cube_mesh();
}