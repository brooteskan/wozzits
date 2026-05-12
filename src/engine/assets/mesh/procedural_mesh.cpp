// src/engine/assets/mesh/procedural_mesh.cpp

#include <engine/assets/mesh/procedural_mesh.h>

namespace wz::engine::assets
{
    MeshData make_triangle_mesh()
    {
        MeshData mesh{};
        mesh.topology = MeshPrimitiveTopology::TriangleList;
        mesh.index_format = MeshIndexFormat::UInt32;

        MeshVertex a{};
        a.position[0] = 0.0f;
        a.position[1] = 1.0f;
        a.position[2] = 0.0f;
        a.normal[2] = 1.0f;
        a.uv[0] = 0.5f;
        a.uv[1] = 0.0f;

        MeshVertex b{};
        b.position[0] = -1.0f;
        b.position[1] = -1.0f;
        b.position[2] = 0.0f;
        b.normal[2] = 1.0f;
        b.uv[0] = 0.0f;
        b.uv[1] = 1.0f;

        MeshVertex c{};
        c.position[0] = 1.0f;
        c.position[1] = -1.0f;
        c.position[2] = 0.0f;
        c.normal[2] = 1.0f;
        c.uv[0] = 1.0f;
        c.uv[1] = 1.0f;

        mesh.vertices = { a, b, c };
        mesh.indices = { 0u, 1u, 2u };

        return mesh;
    }

    MeshData make_quad_mesh()
    {
        MeshData mesh{};
        mesh.topology = MeshPrimitiveTopology::TriangleList;
        mesh.index_format = MeshIndexFormat::UInt32;

        MeshVertex a{};
        a.position[0] = -1.0f;
        a.position[1] = 1.0f;
        a.position[2] = 0.0f;
        a.normal[2] = 1.0f;
        a.uv[0] = 0.0f;
        a.uv[1] = 0.0f;

        MeshVertex b{};
        b.position[0] = -1.0f;
        b.position[1] = -1.0f;
        b.position[2] = 0.0f;
        b.normal[2] = 1.0f;
        b.uv[0] = 0.0f;
        b.uv[1] = 1.0f;

        MeshVertex c{};
        c.position[0] = 1.0f;
        c.position[1] = -1.0f;
        c.position[2] = 0.0f;
        c.normal[2] = 1.0f;
        c.uv[0] = 1.0f;
        c.uv[1] = 1.0f;

        MeshVertex d{};
        d.position[0] = 1.0f;
        d.position[1] = 1.0f;
        d.position[2] = 0.0f;
        d.normal[2] = 1.0f;
        d.uv[0] = 1.0f;
        d.uv[1] = 0.0f;

        mesh.vertices = { a, b, c, d };
        mesh.indices = {
            0u, 1u, 2u,
            0u, 2u, 3u,
        };

        return mesh;
    }

    MeshData make_cube_mesh()
    {
        MeshData mesh{};
        mesh.topology = MeshPrimitiveTopology::TriangleList;
        mesh.index_format = MeshIndexFormat::UInt32;

        const float p[8][3] = {
            { -1.0f, -1.0f, -1.0f },
            {  1.0f, -1.0f, -1.0f },
            {  1.0f,  1.0f, -1.0f },
            { -1.0f,  1.0f, -1.0f },
            { -1.0f, -1.0f,  1.0f },
            {  1.0f, -1.0f,  1.0f },
            {  1.0f,  1.0f,  1.0f },
            { -1.0f,  1.0f,  1.0f },
        };

        for (const auto& pos : p) {
            MeshVertex v{};
            v.position[0] = pos[0];
            v.position[1] = pos[1];
            v.position[2] = pos[2];

            // Temporary V1 normal. This is valid storage, not final smooth/flat
            // normal generation. We can improve this later when vertex layout
            // semantics matter.
            v.normal[2] = 1.0f;

            mesh.vertices.push_back(v);
        }

        mesh.indices = {
            // Back
            0u, 2u, 1u,
            0u, 3u, 2u,

            // Front
            4u, 5u, 6u,
            4u, 6u, 7u,

            // Left
            0u, 4u, 7u,
            0u, 7u, 3u,

            // Right
            1u, 2u, 6u,
            1u, 6u, 5u,

            // Top
            3u, 7u, 6u,
            3u, 6u, 2u,

            // Bottom
            0u, 1u, 5u,
            0u, 5u, 4u,
        };

        return mesh;
    }
}