// src/engine/mesh_processing/mesh_processing_pmp.cpp

#include <engine/mesh_processing/mesh_processing.h>

#include <pmp/algorithms/decimation.h>
#include <pmp/surface_mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wz::engine::mesh_processing
{
    namespace
    {
        using MeshData = wz::engine::assets::MeshData;
        using MeshVertex = wz::engine::assets::MeshVertex;

        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        [[nodiscard]] Vec3 sub(Vec3 a, Vec3 b) noexcept
        {
            return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z };
        }

        [[nodiscard]] Vec3 cross(Vec3 a, Vec3 b) noexcept
        {
            return Vec3{
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x,
            };
        }

        [[nodiscard]] float length(Vec3 v) noexcept
        {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }

        [[nodiscard]] Vec3 normalized(Vec3 v) noexcept
        {
            const float len = length(v);
            if (len <= std::numeric_limits<float>::epsilon())
                return Vec3{ 0.0f, 1.0f, 0.0f };
            return Vec3{ v.x / len, v.y / len, v.z / len };
        }

        [[nodiscard]] Vec3 position_of(const MeshVertex& v) noexcept
        {
            return Vec3{
                v.position[0],
                v.position[1],
                v.position[2],
            };
        }

        void add_normal(MeshVertex& vertex, Vec3 n) noexcept
        {
            vertex.normal[0] += n.x;
            vertex.normal[1] += n.y;
            vertex.normal[2] += n.z;
        }

        void recompute_normals(MeshData& mesh)
        {
            for (MeshVertex& vertex : mesh.vertices)
                vertex.normal[0] = vertex.normal[1] = vertex.normal[2] = 0.0f;

            for (size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
                const uint32_t ia = mesh.indices[i + 0u];
                const uint32_t ib = mesh.indices[i + 1u];
                const uint32_t ic = mesh.indices[i + 2u];
                if (ia >= mesh.vertices.size() ||
                    ib >= mesh.vertices.size() ||
                    ic >= mesh.vertices.size())
                    continue;

                const Vec3 a = position_of(mesh.vertices[ia]);
                const Vec3 b = position_of(mesh.vertices[ib]);
                const Vec3 c = position_of(mesh.vertices[ic]);
                const Vec3 n = cross(sub(b, a), sub(c, a));
                add_normal(mesh.vertices[ia], n);
                add_normal(mesh.vertices[ib], n);
                add_normal(mesh.vertices[ic], n);
            }

            for (MeshVertex& vertex : mesh.vertices) {
                const Vec3 n = normalized(Vec3{
                    vertex.normal[0],
                    vertex.normal[1],
                    vertex.normal[2],
                    });
                vertex.normal[0] = n.x;
                vertex.normal[1] = n.y;
                vertex.normal[2] = n.z;
            }

            mesh.has_normals = true;
        }

        [[nodiscard]] uint32_t requested_target_vertex_count(
            const MeshData& source,
            const MeshDecimationDesc& desc) noexcept
        {
            const uint32_t source_vertices =
                static_cast<uint32_t>(source.vertices.size());

            uint32_t target = desc.target_vertex_count;
            if (target == 0u && desc.target_triangle_count > 0u) {
                const uint32_t source_tris =
                    static_cast<uint32_t>(source.indices.size() / 3u);
                if (source_tris > 0u) {
                    target = static_cast<uint32_t>(
                        std::ceil(
                            static_cast<float>(source_vertices) *
                            static_cast<float>(desc.target_triangle_count) /
                            static_cast<float>(source_tris)));
                }
            }
            if (target == 0u && desc.target_ratio > 0.0f) {
                target = static_cast<uint32_t>(
                    std::ceil(
                        static_cast<float>(source_vertices) *
                        std::clamp(desc.target_ratio, 0.0f, 1.0f)));
            }

            if (target == 0u)
                return source_vertices;

            return std::clamp(target, 3u, source_vertices);
        }

        [[nodiscard]] MeshProcessingResult make_error(std::string message)
        {
            return MeshProcessingResult{
                .ok = false,
                .mesh = {},
                .error = std::move(message),
            };
        }

        [[nodiscard]] MeshProcessingResult make_unchanged(
            const MeshData& source)
        {
            return MeshProcessingResult{
                .ok = true,
                .mesh = source,
                .error = {},
            };
        }
    }

    MeshProcessingResult decimate_mesh(
        const MeshData& source,
        const MeshDecimationDesc& desc)
    {
        if (!source.valid())
            return make_error("source mesh is invalid");
        if (source.indices.size() % 3u != 0u)
            return make_error("source mesh is not a triangle list");

        const uint32_t target_vertices =
            requested_target_vertex_count(source, desc);
        if (target_vertices >= source.vertices.size())
            return make_unchanged(source);

        pmp::SurfaceMesh pmp_mesh;
        std::vector<pmp::Vertex> vertices;
        vertices.reserve(source.vertices.size());

        for (const MeshVertex& vertex : source.vertices) {
            vertices.push_back(
                pmp_mesh.add_vertex(pmp::Point{
                    vertex.position[0],
                    vertex.position[1],
                    vertex.position[2],
                    }));
        }

        for (size_t i = 0; i + 2u < source.indices.size(); i += 3u) {
            const uint32_t ia = source.indices[i + 0u];
            const uint32_t ib = source.indices[i + 1u];
            const uint32_t ic = source.indices[i + 2u];
            if (ia >= vertices.size() || ib >= vertices.size() ||
                ic >= vertices.size()) {
                return make_error("source mesh has an out-of-range index");
            }
            if (ia == ib || ib == ic || ia == ic)
                continue;

            const pmp::Face face =
                pmp_mesh.add_triangle(vertices[ia], vertices[ib], vertices[ic]);
            if (!face.is_valid())
                return make_error("source mesh cannot be represented by PMP");
        }

        if (pmp_mesh.n_faces() == 0u)
            return make_error("source mesh has no valid triangles");

        if (desc.preserve_boundary) {
            auto selected =
                pmp_mesh.vertex_property<bool>("v:selected", true);
            for (auto v : pmp_mesh.vertices())
                selected[v] = !pmp_mesh.is_boundary(v);
        }

        try {
            pmp::decimate(
                pmp_mesh,
                target_vertices,
                desc.aspect_ratio,
                desc.edge_length,
                desc.max_valence,
                desc.normal_deviation,
                desc.hausdorff_error);
        } catch (...) {
            return make_error("PMP decimation failed");
        }

        MeshData out{};
        out.topology = wz::engine::assets::MeshPrimitiveTopology::TriangleList;
        out.index_format = wz::engine::assets::MeshIndexFormat::UInt32;
        out.has_uv0 = false;

        std::unordered_map<uint32_t, uint32_t> out_index_by_pmp_vertex;
        out_index_by_pmp_vertex.reserve(pmp_mesh.n_vertices());

        for (auto v : pmp_mesh.vertices()) {
            const pmp::Point& p = pmp_mesh.position(v);

            MeshVertex vertex{};
            vertex.position[0] = p[0];
            vertex.position[1] = p[1];
            vertex.position[2] = p[2];

            const uint32_t out_index =
                static_cast<uint32_t>(out.vertices.size());
            out.vertices.push_back(vertex);
            out_index_by_pmp_vertex.emplace(v.idx(), out_index);
        }

        for (auto f : pmp_mesh.faces()) {
            std::array<uint32_t, 3> face_indices{};
            uint32_t count = 0;
            for (auto v : pmp_mesh.vertices(f)) {
                if (count >= face_indices.size())
                    return make_error("PMP output produced a non-triangle face");
                const auto it = out_index_by_pmp_vertex.find(v.idx());
                if (it == out_index_by_pmp_vertex.end())
                    return make_error("PMP output references an unknown vertex");
                face_indices[count++] = it->second;
            }
            if (count != 3u)
                return make_error("PMP output produced a non-triangle face");
            if (face_indices[0] == face_indices[1] ||
                face_indices[1] == face_indices[2] ||
                face_indices[0] == face_indices[2])
                continue;

            out.indices.push_back(face_indices[0]);
            out.indices.push_back(face_indices[1]);
            out.indices.push_back(face_indices[2]);
        }

        recompute_normals(out);

        if (!out.valid())
            return make_error("PMP decimation produced invalid mesh data");

        return MeshProcessingResult{
            .ok = true,
            .mesh = std::move(out),
            .error = {},
        };
    }
}
