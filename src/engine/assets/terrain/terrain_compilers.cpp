// src/engine/assets/terrain/terrain_compilers.cpp

#include <engine/assets/terrain/terrain_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        void copy_mesh_bounds(
            float dst_min[3],
            float dst_max[3],
            const MeshData& mesh)
        {
            for (int axis = 0; axis < 3; ++axis) {
                dst_min[axis] = mesh.vertices[0].position[axis];
                dst_max[axis] = mesh.vertices[0].position[axis];
            }

            for (const auto& vertex : mesh.vertices) {
                for (int axis = 0; axis < 3; ++axis) {
                    dst_min[axis] =
                        std::min(dst_min[axis], vertex.position[axis]);
                    dst_max[axis] =
                        std::max(dst_max[axis], vertex.position[axis]);
                }
            }
        }

        TerrainNormalSource choose_terrain_normal_source(
            TerrainNormalSource preferred,
            const MeshData& mesh) noexcept
        {
            if (preferred == TerrainNormalSource::MeshVertexNormal
                && mesh.has_normals)
            {
                return TerrainNormalSource::MeshVertexNormal;
            }
            if (preferred == TerrainNormalSource::ImportedField) {
                return TerrainNormalSource::ImportedField;
            }
            return TerrainNormalSource::DerivedGeometry;
        }

        TerrainUVSource choose_terrain_uv_source(
            TerrainUVSource preferred,
            const MeshData& mesh) noexcept
        {
            if (preferred == TerrainUVSource::MeshUV0 && mesh.has_uv0) {
                return TerrainUVSource::MeshUV0;
            }
            if (preferred == TerrainUVSource::ImportedField) {
                return TerrainUVSource::ImportedField;
            }
            if (preferred == TerrainUVSource::PlanarXZ) {
                return TerrainUVSource::PlanarXZ;
            }
            return TerrainUVSource::None;
        }

        float mesh_triangle_normal_y(
            const MeshData& mesh,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic) noexcept
        {
            const auto& a = mesh.vertices[ia];
            const auto& b = mesh.vertices[ib];
            const auto& c = mesh.vertices[ic];

            const float abx = b.position[0] - a.position[0];
            const float aby = b.position[1] - a.position[1];
            const float abz = b.position[2] - a.position[2];
            const float acx = c.position[0] - a.position[0];
            const float acy = c.position[1] - a.position[1];
            const float acz = c.position[2] - a.position[2];

            const float nx = aby * acz - abz * acy;
            const float ny = abz * acx - abx * acz;
            const float nz = abx * acy - aby * acx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len <= 0.0f) {
                return 0.0f;
            }
            return ny / len;
        }

        uint32_t count_accepted_mesh_surface_triangles(
            const MeshData& mesh,
            float min_surface_normal_y,
            bool include_backfaces) noexcept
        {
            uint32_t accepted = 0;
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const uint32_t ia = mesh.indices[i + 0];
                const uint32_t ib = mesh.indices[i + 1];
                const uint32_t ic = mesh.indices[i + 2];
                if (ia >= mesh.vertices.size()
                    || ib >= mesh.vertices.size()
                    || ic >= mesh.vertices.size())
                {
                    continue;
                }

                const float normal_y = mesh_triangle_normal_y(mesh, ia, ib, ic);
                const float comparable_y =
                    include_backfaces ? std::abs(normal_y) : normal_y;
                if (comparable_y >= min_surface_normal_y) {
                    ++accepted;
                }
            }
            return accepted;
        }

        struct ChunkBuildState
        {
            std::vector<uint32_t> indices;
            TerrainVisualChunk chunk{};
            double area_sum = 0.0;
            double height_sum = 0.0;
            double height_sq_sum = 0.0;
            double normal_sum[3]{};
            double normal_sq_sum[3]{};
            bool initialized_bounds = false;
        };

        float triangle_area_and_normal(
            const MeshData& mesh,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic,
            float normal[3]) noexcept
        {
            const auto& a = mesh.vertices[ia];
            const auto& b = mesh.vertices[ib];
            const auto& c = mesh.vertices[ic];

            const float abx = b.position[0] - a.position[0];
            const float aby = b.position[1] - a.position[1];
            const float abz = b.position[2] - a.position[2];
            const float acx = c.position[0] - a.position[0];
            const float acy = c.position[1] - a.position[1];
            const float acz = c.position[2] - a.position[2];

            float nx = aby * acz - abz * acy;
            float ny = abz * acx - abx * acz;
            float nz = abx * acy - aby * acx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len <= 0.0f) {
                normal[0] = 0.0f;
                normal[1] = 1.0f;
                normal[2] = 0.0f;
                return 0.0f;
            }

            const float inv_len = 1.0f / len;
            normal[0] = nx * inv_len;
            normal[1] = ny * inv_len;
            normal[2] = nz * inv_len;
            return 0.5f * len;
        }

        void expand_chunk_bounds(
            ChunkBuildState& chunk,
            const MeshData& mesh,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic)
        {
            const uint32_t ids[3]{ ia, ib, ic };
            for (uint32_t id : ids) {
                const auto& vertex = mesh.vertices[id];
                if (!chunk.initialized_bounds) {
                    for (int axis = 0; axis < 3; ++axis) {
                        chunk.chunk.bounds_min[axis] = vertex.position[axis];
                        chunk.chunk.bounds_max[axis] = vertex.position[axis];
                    }
                    chunk.initialized_bounds = true;
                    continue;
                }

                for (int axis = 0; axis < 3; ++axis) {
                    chunk.chunk.bounds_min[axis] = std::min(
                        chunk.chunk.bounds_min[axis],
                        vertex.position[axis]);
                    chunk.chunk.bounds_max[axis] = std::max(
                        chunk.chunk.bounds_max[axis],
                        vertex.position[axis]);
                }
            }
        }

        void accumulate_chunk_aggregate(
            ChunkBuildState& chunk,
            const MeshData& mesh,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic)
        {
            float normal[3]{};
            const float area = triangle_area_and_normal(
                mesh,
                ia,
                ib,
                ic,
                normal);
            const double weight = area > 0.0f ? static_cast<double>(area) : 1.0;
            const auto& a = mesh.vertices[ia];
            const auto& b = mesh.vertices[ib];
            const auto& c = mesh.vertices[ic];
            const double centroid_y =
                (static_cast<double>(a.position[1])
                    + static_cast<double>(b.position[1])
                    + static_cast<double>(c.position[1])) / 3.0;

            chunk.area_sum += weight;
            chunk.height_sum += centroid_y * weight;
            chunk.height_sq_sum += centroid_y * centroid_y * weight;
            for (int axis = 0; axis < 3; ++axis) {
                const double n = normal[axis];
                chunk.normal_sum[axis] += n * weight;
                chunk.normal_sq_sum[axis] += n * n * weight;
            }
            ++chunk.chunk.aggregate.triangle_count;
        }

        void finalize_chunk_aggregate(ChunkBuildState& state)
        {
            TerrainVisualChunkAggregate& out = state.chunk.aggregate;
            if (state.area_sum <= 0.0) {
                return;
            }

            out.mean_height =
                static_cast<float>(state.height_sum / state.area_sum);
            const double height_mean = out.mean_height;
            out.height_variance = static_cast<float>(
                std::max(
                    0.0,
                    state.height_sq_sum / state.area_sum
                        - height_mean * height_mean));

            double normal_len_sq = 0.0;
            double normal_mean[3]{};
            for (int axis = 0; axis < 3; ++axis) {
                normal_mean[axis] = state.normal_sum[axis] / state.area_sum;
                normal_len_sq += normal_mean[axis] * normal_mean[axis];
            }
            const double normal_len = std::sqrt(normal_len_sq);
            if (normal_len > 1e-9) {
                for (int axis = 0; axis < 3; ++axis) {
                    out.normal_mean[axis] =
                        static_cast<float>(normal_mean[axis] / normal_len);
                }
            }

            const double var_x = std::max(
                0.0,
                state.normal_sq_sum[0] / state.area_sum
                    - normal_mean[0] * normal_mean[0]);
            const double var_z = std::max(
                0.0,
                state.normal_sq_sum[2] / state.area_sum
                    - normal_mean[2] * normal_mean[2]);
            out.normal_variance[0] = static_cast<float>(var_x);
            out.normal_variance[1] = static_cast<float>(var_z);
        }

        struct TriangleCentroid
        {
            uint32_t tri_index;
            float cx;
            float cz;
        };

        constexpr uint32_t kDefaultVisualChunkCount = 4096u;

        void kd_split_visual_chunks(
            const MeshData& mesh,
            std::vector<TriangleCentroid>& centroids,
            size_t begin,
            size_t end,
            uint32_t target_triangles,
            std::vector<ChunkBuildState>& out_chunks)
        {
            const size_t count = end - begin;
            if (count == 0) {
                return;
            }

            if (count <= target_triangles) {
                ChunkBuildState chunk{};
                chunk.indices.reserve(count * 3u);
                for (size_t i = begin; i < end; ++i) {
                    const size_t base =
                        static_cast<size_t>(centroids[i].tri_index) * 3u;
                    const uint32_t ia = mesh.indices[base + 0];
                    const uint32_t ib = mesh.indices[base + 1];
                    const uint32_t ic = mesh.indices[base + 2];
                    chunk.indices.push_back(ia);
                    chunk.indices.push_back(ib);
                    chunk.indices.push_back(ic);
                    expand_chunk_bounds(chunk, mesh, ia, ib, ic);
                    accumulate_chunk_aggregate(chunk, mesh, ia, ib, ic);
                }
                out_chunks.push_back(std::move(chunk));
                return;
            }

            float min_x = centroids[begin].cx;
            float max_x = min_x;
            float min_z = centroids[begin].cz;
            float max_z = min_z;
            for (size_t i = begin + 1; i < end; ++i) {
                min_x = std::min(min_x, centroids[i].cx);
                max_x = std::max(max_x, centroids[i].cx);
                min_z = std::min(min_z, centroids[i].cz);
                max_z = std::max(max_z, centroids[i].cz);
            }

            const size_t mid = begin + count / 2u;
            if ((max_x - min_x) >= (max_z - min_z)) {
                std::nth_element(
                    centroids.begin() + static_cast<ptrdiff_t>(begin),
                    centroids.begin() + static_cast<ptrdiff_t>(mid),
                    centroids.begin() + static_cast<ptrdiff_t>(end),
                    [](const TriangleCentroid& a,
                       const TriangleCentroid& b) {
                        return a.cx < b.cx;
                    });
            }
            else {
                std::nth_element(
                    centroids.begin() + static_cast<ptrdiff_t>(begin),
                    centroids.begin() + static_cast<ptrdiff_t>(mid),
                    centroids.begin() + static_cast<ptrdiff_t>(end),
                    [](const TriangleCentroid& a,
                       const TriangleCentroid& b) {
                        return a.cz < b.cz;
                    });
            }

            kd_split_visual_chunks(
                mesh, centroids, begin, mid,
                target_triangles, out_chunks);
            kd_split_visual_chunks(
                mesh, centroids, mid, end,
                target_triangles, out_chunks);
        }

        bool mesh_triangle_is_accepted_surface(
            const MeshData& mesh,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic,
            float min_surface_normal_y,
            bool include_backfaces) noexcept
        {
            const float normal_y = mesh_triangle_normal_y(mesh, ia, ib, ic);
            const float comparable_y =
                include_backfaces ? std::abs(normal_y) : normal_y;
            return comparable_y >= min_surface_normal_y;
        }

        void copy_accepted_mesh_surface(
            TerrainAssetData& data,
            const MeshData& mesh,
            float min_surface_normal_y,
            bool include_backfaces)
        {
            data.mesh_surface_points.reserve(mesh.vertices.size() * 3u);
            for (const auto& vertex : mesh.vertices) {
                data.mesh_surface_points.push_back(vertex.position[0]);
                data.mesh_surface_points.push_back(vertex.position[1]);
                data.mesh_surface_points.push_back(vertex.position[2]);
            }

            data.mesh_surface_indices.clear();
            data.mesh_surface_indices.reserve(mesh.indices.size());

            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const uint32_t ia = mesh.indices[i + 0];
                const uint32_t ib = mesh.indices[i + 1];
                const uint32_t ic = mesh.indices[i + 2];
                if (ia >= mesh.vertices.size()
                    || ib >= mesh.vertices.size()
                    || ic >= mesh.vertices.size())
                {
                    continue;
                }

                if (!mesh_triangle_is_accepted_surface(
                        mesh,
                        ia,
                        ib,
                        ic,
                        min_surface_normal_y,
                        include_backfaces))
                {
                    continue;
                }

                data.mesh_surface_indices.push_back(ia);
                data.mesh_surface_indices.push_back(ib);
                data.mesh_surface_indices.push_back(ic);
            }
        }

        void copy_chunked_visual_mesh_surface(
            TerrainAssetData& data,
            const MeshData& mesh,
            uint32_t visual_chunk_count)
        {
            const uint32_t triangle_count =
                static_cast<uint32_t>(mesh.indices.size() / 3u);

            data.mesh_visual_indices.clear();
            data.mesh_visual_chunks.clear();

            if (triangle_count == 0) {
                return;
            }

            const uint32_t chunk_count =
                visual_chunk_count == 0u
                    ? kDefaultVisualChunkCount
                    : visual_chunk_count;
            const uint32_t target_triangles_per_chunk = std::max(
                1u,
                (triangle_count + chunk_count - 1u) / chunk_count);

            std::vector<TriangleCentroid> centroids;
            centroids.reserve(triangle_count);
            for (uint32_t t = 0; t < triangle_count; ++t) {
                const size_t base = static_cast<size_t>(t) * 3u;
                const uint32_t ia = mesh.indices[base + 0];
                const uint32_t ib = mesh.indices[base + 1];
                const uint32_t ic = mesh.indices[base + 2];
                if (ia >= mesh.vertices.size()
                    || ib >= mesh.vertices.size()
                    || ic >= mesh.vertices.size())
                {
                    continue;
                }
                const auto& a = mesh.vertices[ia];
                const auto& b = mesh.vertices[ib];
                const auto& c = mesh.vertices[ic];
                centroids.push_back({
                    t,
                    (a.position[0] + b.position[0] + c.position[0]) / 3.0f,
                    (a.position[2] + b.position[2] + c.position[2]) / 3.0f,
                });
            }

            std::vector<ChunkBuildState> chunks;
            kd_split_visual_chunks(
                mesh,
                centroids,
                0,
                centroids.size(),
                target_triangles_per_chunk,
                chunks);

            data.mesh_visual_indices.reserve(mesh.indices.size());
            data.mesh_visual_chunks.reserve(chunks.size());

            for (ChunkBuildState& chunk : chunks) {
                if (chunk.indices.empty()) {
                    continue;
                }

                finalize_chunk_aggregate(chunk);
                chunk.chunk.first_index =
                    static_cast<uint32_t>(data.mesh_visual_indices.size());
                chunk.chunk.index_count =
                    static_cast<uint32_t>(chunk.indices.size());
                data.mesh_visual_indices.insert(
                    data.mesh_visual_indices.end(),
                    chunk.indices.begin(),
                    chunk.indices.end());
                data.mesh_visual_chunks.push_back(chunk.chunk);
            }
        }
    }

    void register_terrain_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_fields_table,
        MeshTable& mesh_table,
        TerrainAssetTable& terrain_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTerrainFromHeightFieldSchema,
            .output_type = kAssetTypeTerrain,
            .compile = [&logger, &scalar_fields_table, &terrain_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<TerrainFromHeightFieldCompileDesc>(
                        &input.meta);
                if (!desc) {
                    logger.error("terrain heightfield missing compile desc");
                    return compile_failed_node(input);
                }
                if (dep_handles.size() != 1) {
                    logger.error(
                        "terrain heightfield requires one scalar field dependency");
                    return compile_failed_node(input);
                }

                const ScalarFieldData* field =
                    scalar_fields_table.get(dep_handles[0]);
                if (!field || !field->valid()) {
                    logger.error("terrain heightfield source is invalid");
                    return compile_failed_node(input);
                }
                if (field->depth != 1) {
                    logger.error("terrain heightfield source must be 2D");
                    return compile_failed_node(input);
                }
                if (desc->size[0] <= 0.0f || desc->size[1] <= 0.0f) {
                    logger.error("terrain heightfield size must be positive");
                    return compile_failed_node(input);
                }

                const float min_h =
                    desc->base_height + field->min_value * desc->vertical_scale;
                const float max_h =
                    desc->base_height + field->max_value * desc->vertical_scale;

                TerrainAssetData data{};
                data.representation = TerrainRepresentationKind::HeightField;
                data.source_asset = desc->height_field;
                data.height_field = desc->height_field;
                data.normal_field = desc->normal_field;
                data.material_mask_set = desc->material_mask_set;
                data.origin[0] = desc->origin[0];
                data.origin[1] = desc->origin[1];
                data.size[0] = desc->size[0];
                data.size[1] = desc->size[1];
                data.resolution_x = field->width;
                data.resolution_y = field->height;
                data.vertical_scale = desc->vertical_scale;
                data.base_height = desc->base_height;
                data.min_height = std::min(min_h, max_h);
                data.max_height = std::max(min_h, max_h);
                data.height_samples = field->values;
                data.bounds_min[0] = desc->origin[0];
                data.bounds_min[1] = data.min_height;
                data.bounds_min[2] = desc->origin[1];
                data.bounds_max[0] = desc->origin[0] + desc->size[0];
                data.bounds_max[1] = data.max_height;
                data.bounds_max[2] = desc->origin[1] + desc->size[1];
                data.render_mode = desc->render_mode;
                data.collision_mode = desc->collision_mode;
                data.supports_height_query = true;
                data.supports_ray_query = false;
                data.supports_render_mesh =
                    desc->render_mode == TerrainRenderMode::DebugMesh;

                wz::asset::ResourceHandle handle =
                    terrain_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store heightfield terrain");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTerrainFromMeshSchema,
            .output_type = kAssetTypeTerrain,
            .compile = [&logger, &mesh_table, &terrain_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<TerrainFromMeshCompileDesc>(&input.meta);
                if (!desc) {
                    logger.error("terrain mesh missing compile desc");
                    return compile_failed_node(input);
                }
                if (dep_handles.size() != 1) {
                    logger.error("terrain mesh requires one mesh dependency");
                    return compile_failed_node(input);
                }

                const MeshData* mesh = mesh_table.get(dep_handles[0]);
                if (!mesh || !mesh->valid()) {
                    logger.error("terrain mesh source is invalid");
                    return compile_failed_node(input);
                }

                TerrainAssetData data{};
                data.representation = TerrainRepresentationKind::MeshSurface;
                data.source_asset = desc->mesh;
                data.mesh = desc->mesh;
                data.mesh_height_policy = desc->height_policy;
                data.min_surface_normal_y = desc->min_surface_normal_y;
                data.include_backfaces = desc->include_backfaces;
                data.mesh_has_source_normals = mesh->has_normals;
                data.mesh_has_source_uv0 = mesh->has_uv0;
                data.mesh_visual_chunk_count =
                    desc->visual_chunk_count == 0u
                        ? kDefaultVisualChunkCount
                        : desc->visual_chunk_count;
                data.mesh_triangle_count =
                    static_cast<uint32_t>(mesh->indices.size() / 3u);
                data.mesh_accepted_surface_triangle_count =
                    count_accepted_mesh_surface_triangles(
                        *mesh,
                        desc->min_surface_normal_y,
                        desc->include_backfaces);
                data.normal_source = choose_terrain_normal_source(
                    desc->preferred_normal_source,
                    *mesh);
                data.uv_source = choose_terrain_uv_source(
                    desc->preferred_uv_source,
                    *mesh);
                copy_mesh_bounds(data.bounds_min, data.bounds_max, *mesh);
                copy_accepted_mesh_surface(
                    data,
                    *mesh,
                    desc->min_surface_normal_y,
                    desc->include_backfaces);
                copy_chunked_visual_mesh_surface(
                    data,
                    *mesh,
                    data.mesh_visual_chunk_count);
                data.origin[0] = data.bounds_min[0];
                data.origin[1] = data.bounds_min[2];
                data.size[0] = data.bounds_max[0] - data.bounds_min[0];
                data.size[1] = data.bounds_max[2] - data.bounds_min[2];
                data.min_height = data.bounds_min[1];
                data.max_height = data.bounds_max[1];
                data.render_mode = desc->render_mode;
                data.collision_mode = desc->collision_mode;
                data.supports_height_query = false;
                data.supports_ray_query = false;
                data.supports_render_mesh =
                    desc->render_mode == TerrainRenderMode::DebugMesh;

                wz::asset::ResourceHandle handle =
                    terrain_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store mesh terrain");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });
    }
}
