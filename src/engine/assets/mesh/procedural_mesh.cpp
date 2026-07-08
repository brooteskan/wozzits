// src/engine/assets/mesh/procedural_mesh.cpp

#include <engine/assets/mesh/procedural_mesh.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

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
        mesh.has_normals = true;
        mesh.has_uv0 = true;

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
        mesh.has_normals = true;
        mesh.has_uv0 = true;

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
        mesh.has_normals = true;
        mesh.has_uv0 = false;

        return mesh;
    }

    MeshData make_sphere_mesh(uint32_t subdivisions, float radius)
    {
        MeshData mesh{};
        mesh.topology = MeshPrimitiveTopology::TriangleList;
        mesh.index_format = MeshIndexFormat::UInt32;

        // Regular icosahedron (12 vertices, 20 faces) built from the golden
        // ratio. Only the vertex DIRECTIONS matter here — every position is
        // projected onto the sphere below, so the starting magnitude is
        // irrelevant.
        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;

        std::vector<std::array<float, 3>> positions = {
            { -1.0f,  t,     0.0f },
            {  1.0f,  t,     0.0f },
            { -1.0f, -t,     0.0f },
            {  1.0f, -t,     0.0f },
            {  0.0f, -1.0f,  t    },
            {  0.0f,  1.0f,  t    },
            {  0.0f, -1.0f, -t    },
            {  0.0f,  1.0f, -t    },
            {  t,     0.0f, -1.0f },
            {  t,     0.0f,  1.0f },
            { -t,     0.0f, -1.0f },
            { -t,     0.0f,  1.0f },
        };

        // Faces wound so that (p1 - p0) x (p2 - p0) points OUTWARD, matching the
        // engine's cube convention (index-order face normal points away from
        // the center).
        std::vector<std::array<uint32_t, 3>> faces = {
            { 0u, 11u, 5u }, { 0u, 5u, 1u },  { 0u, 1u, 7u },
            { 0u, 7u, 10u }, { 0u, 10u, 11u },
            { 1u, 5u, 9u },  { 5u, 11u, 4u }, { 11u, 10u, 2u },
            { 10u, 7u, 6u }, { 7u, 1u, 8u },
            { 3u, 9u, 4u },  { 3u, 4u, 2u },  { 3u, 2u, 6u },
            { 3u, 6u, 8u },  { 3u, 8u, 9u },
            { 4u, 9u, 5u },  { 2u, 4u, 11u }, { 6u, 2u, 10u },
            { 8u, 6u, 7u },  { 9u, 8u, 1u },
        };

        // Undirected-edge -> inserted-midpoint index, so adjacent faces share
        // the split vertex: crack-free, no duplicated vertices.
        std::unordered_map<uint64_t, uint32_t> midpoint_cache;
        const auto midpoint = [&](uint32_t a, uint32_t b) -> uint32_t {
            const uint64_t key = a < b
                ? (static_cast<uint64_t>(a) << 32) | b
                : (static_cast<uint64_t>(b) << 32) | a;
            const auto it = midpoint_cache.find(key);
            if (it != midpoint_cache.end()) {
                return it->second;
            }
            const std::array<float, 3>& pa = positions[a];
            const std::array<float, 3>& pb = positions[b];
            const uint32_t index = static_cast<uint32_t>(positions.size());
            positions.push_back({
                (pa[0] + pb[0]) * 0.5f,
                (pa[1] + pb[1]) * 0.5f,
                (pa[2] + pb[2]) * 0.5f,
            });
            midpoint_cache.emplace(key, index);
            return index;
        };

        for (uint32_t s = 0; s < subdivisions; ++s) {
            std::vector<std::array<uint32_t, 3>> next;
            next.reserve(faces.size() * 4u);
            for (const std::array<uint32_t, 3>& f : faces) {
                const uint32_t a = midpoint(f[0], f[1]);
                const uint32_t b = midpoint(f[1], f[2]);
                const uint32_t c = midpoint(f[2], f[0]);
                // Four sub-triangles, each preserving the parent winding.
                next.push_back({ f[0], a, c });
                next.push_back({ f[1], b, a });
                next.push_back({ f[2], c, b });
                next.push_back({ a, b, c });
            }
            faces = std::move(next);
        }

        // Project every vertex onto the sphere of the requested radius; the
        // normalized direction is exactly the outward normal for a sphere.
        mesh.vertices.reserve(positions.size());
        for (const std::array<float, 3>& p : positions) {
            const float length =
                std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            const float inv = length > 0.0f ? 1.0f / length : 0.0f;

            MeshVertex v{};
            v.position[0] = p[0] * inv * radius;
            v.position[1] = p[1] * inv * radius;
            v.position[2] = p[2] * inv * radius;
            v.normal[0] = p[0] * inv;
            v.normal[1] = p[1] * inv;
            v.normal[2] = p[2] * inv;
            mesh.vertices.push_back(v);
        }

        mesh.indices.reserve(faces.size() * 3u);
        for (const std::array<uint32_t, 3>& f : faces) {
            mesh.indices.push_back(f[0]);
            mesh.indices.push_back(f[1]);
            mesh.indices.push_back(f[2]);
        }

        mesh.has_normals = true;
        mesh.has_uv0 = false;

        return mesh;
    }
}
