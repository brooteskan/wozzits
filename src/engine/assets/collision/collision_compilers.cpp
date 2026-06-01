// src/engine/assets/collision/collision_compilers.cpp

#include <engine/assets/collision/collision_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <cfloat>
#include <cmath>
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

        void copy_bounds(
            float dst_min[3],
            float dst_max[3],
            const float src_min[3],
            const float src_max[3])
        {
            for (int axis = 0; axis < 3; ++axis) {
                dst_min[axis] = src_min[axis];
                dst_max[axis] = src_max[axis];
            }
        }

        CollisionAssetData collision_from_mesh(
            const CollisionFromMeshCompileDesc& desc,
            const MeshData& mesh)
        {
            CollisionAssetData data{};
            data.source_kind = CollisionSourceKind::Mesh;
            data.source_asset = desc.mesh;
            data.geometry_asset = desc.mesh;
            data.mesh = desc.mesh;
            data.occupancy = desc.occupancy;
            data.source_triangle_count =
                static_cast<uint32_t>(mesh.indices.size() / 3u);
            data.accepted_triangle_count = data.source_triangle_count;
            copy_mesh_bounds(data.bounds_min, data.bounds_max, mesh);

            if (desc.build_method == CollisionBuildMethod::Bounds) {
                data.shape_kind = CollisionShapeKind::Bounds;
                data.supports_overlap_query = true;
                return data;
            }

            data.shape_kind = CollisionShapeKind::TriangleMesh;
            data.points.reserve(mesh.vertices.size());
            for (const auto& vertex : mesh.vertices) {
                CollisionPoint point{};
                point.position[0] = vertex.position[0];
                point.position[1] = vertex.position[1];
                point.position[2] = vertex.position[2];
                data.points.push_back(point);
            }
            data.indices = mesh.indices;
            data.supports_ray_query = true;
            data.supports_overlap_query =
                desc.occupancy.kind == CollisionOccupancyKind::Solid;
            return data;
        }

        void build_triangle_bounds_and_grid(CollisionAssetData& data)
        {
            const uint32_t triangle_count =
                static_cast<uint32_t>(data.indices.size() / 3u);
            data.triangle_bounds.clear();
            data.triangle_bounds.reserve(triangle_count);

            std::vector<std::vector<uint32_t>> cell_triangles;
            const float span_x = data.bounds_max[0] - data.bounds_min[0];
            const float span_z = data.bounds_max[2] - data.bounds_min[2];
            if (triangle_count > 0 && span_x > 0.0f && span_z > 0.0f) {
                const uint32_t cells =
                    (std::min)(
                        128u,
                        (std::max)(
                            1u,
                            static_cast<uint32_t>(
                                std::sqrt(
                                    static_cast<float>(triangle_count) / 8.0f))));
                data.surface_grid.origin_x = data.bounds_min[0];
                data.surface_grid.origin_z = data.bounds_min[2];
                data.surface_grid.cells_x = cells;
                data.surface_grid.cells_z = cells;
                data.surface_grid.cell_size_x = span_x / static_cast<float>(cells);
                data.surface_grid.cell_size_z = span_z / static_cast<float>(cells);
                cell_triangles.resize(static_cast<size_t>(cells) * cells);
                data.surface_grid.cell_bounds.resize(cell_triangles.size());
                for (auto& bounds : data.surface_grid.cell_bounds) {
                    bounds.min[0] = FLT_MAX;
                    bounds.min[1] = FLT_MAX;
                    bounds.min[2] = FLT_MAX;
                    bounds.max[0] = -FLT_MAX;
                    bounds.max[1] = -FLT_MAX;
                    bounds.max[2] = -FLT_MAX;
                }
            }

            for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                const uint32_t ia = data.indices[tri * 3u + 0u];
                const uint32_t ib = data.indices[tri * 3u + 1u];
                const uint32_t ic = data.indices[tri * 3u + 2u];
                if (ia >= data.points.size()
                    || ib >= data.points.size()
                    || ic >= data.points.size())
                {
                    data.triangle_bounds.push_back({});
                    continue;
                }

                const CollisionPoint& a = data.points[ia];
                const CollisionPoint& b = data.points[ib];
                const CollisionPoint& c = data.points[ic];
                CollisionTriangleBounds bounds{};
                for (int axis = 0; axis < 3; ++axis) {
                    bounds.min[axis] =
                        (std::min)({
                            a.position[axis],
                            b.position[axis],
                            c.position[axis],
                        });
                    bounds.max[axis] =
                        (std::max)({
                            a.position[axis],
                            b.position[axis],
                            c.position[axis],
                        });
                }
                data.triangle_bounds.push_back(bounds);

                if (cell_triangles.empty()) {
                    continue;
                }

                auto cell_index = [](float value, float origin, float size, uint32_t count) {
                    const float normalized = (value - origin) / size;
                    const int raw = static_cast<int>(std::floor(normalized));
                    return static_cast<uint32_t>(
                        (std::clamp)(raw, 0, static_cast<int>(count) - 1));
                };

                const uint32_t min_x = cell_index(
                    bounds.min[0],
                    data.surface_grid.origin_x,
                    data.surface_grid.cell_size_x,
                    data.surface_grid.cells_x);
                const uint32_t max_x = cell_index(
                    bounds.max[0],
                    data.surface_grid.origin_x,
                    data.surface_grid.cell_size_x,
                    data.surface_grid.cells_x);
                const uint32_t min_z = cell_index(
                    bounds.min[2],
                    data.surface_grid.origin_z,
                    data.surface_grid.cell_size_z,
                    data.surface_grid.cells_z);
                const uint32_t max_z = cell_index(
                    bounds.max[2],
                    data.surface_grid.origin_z,
                    data.surface_grid.cell_size_z,
                    data.surface_grid.cells_z);

                for (uint32_t z = min_z; z <= max_z; ++z) {
                    for (uint32_t x = min_x; x <= max_x; ++x) {
                        const size_t cell =
                            static_cast<size_t>(z)
                                * data.surface_grid.cells_x
                            + x;
                        cell_triangles[cell].push_back(tri);

                        auto& cell_bounds =
                            data.surface_grid.cell_bounds[cell];
                        for (int axis = 0; axis < 3; ++axis) {
                            cell_bounds.min[axis] =
                                (std::min)(
                                    cell_bounds.min[axis],
                                    bounds.min[axis]);
                            cell_bounds.max[axis] =
                                (std::max)(
                                    cell_bounds.max[axis],
                                    bounds.max[axis]);
                        }
                    }
                }
            }

            if (cell_triangles.empty()) {
                return;
            }

            data.surface_grid.cell_offsets.clear();
            data.surface_grid.cell_offsets.reserve(cell_triangles.size() + 1u);
            data.surface_grid.cell_triangle_indices.clear();
            data.surface_grid.cell_offsets.push_back(0u);
            for (const auto& cell : cell_triangles) {
                data.surface_grid.cell_triangle_indices.insert(
                    data.surface_grid.cell_triangle_indices.end(),
                    cell.begin(),
                    cell.end());
                data.surface_grid.cell_offsets.push_back(
                    static_cast<uint32_t>(
                        data.surface_grid.cell_triangle_indices.size()));
            }
        }

        CollisionAssetData collision_from_terrain(
            const CollisionFromTerrainCompileDesc& desc,
            const TerrainAssetData& terrain)
        {
            CollisionAssetData data{};
            data.source_kind = CollisionSourceKind::Terrain;
            data.source_asset = desc.terrain;
            data.geometry_asset = terrain.source_asset;
            data.occupancy = desc.occupancy;
            copy_bounds(
                data.bounds_min,
                data.bounds_max,
                terrain.bounds_min,
                terrain.bounds_max);

            if (desc.build_method == CollisionBuildMethod::Bounds) {
                data.shape_kind = CollisionShapeKind::Bounds;
                data.supports_overlap_query = true;
                return data;
            }

            if (terrain.representation == TerrainRepresentationKind::HeightField) {
                data.shape_kind = CollisionShapeKind::TerrainHeightField;
                data.height_field = terrain.height_field;
                data.origin[0] = terrain.origin[0];
                data.origin[1] = terrain.origin[1];
                data.size[0] = terrain.size[0];
                data.size[1] = terrain.size[1];
                data.resolution_x = terrain.resolution_x;
                data.resolution_y = terrain.resolution_y;
                data.min_height = terrain.min_height;
                data.max_height = terrain.max_height;
                data.supports_height_query = true;
                data.supports_ray_query = true;
                return data;
            }

            data.shape_kind = CollisionShapeKind::TerrainMeshSurface;
            data.mesh = terrain.mesh;
            data.source_triangle_count = terrain.mesh_triangle_count;
            data.accepted_triangle_count =
                terrain.mesh_accepted_surface_triangle_count;
            data.points.reserve(terrain.mesh_surface_points.size() / 3u);
            for (size_t i = 0; i + 2 < terrain.mesh_surface_points.size(); i += 3) {
                CollisionPoint point{};
                point.position[0] = terrain.mesh_surface_points[i + 0];
                point.position[1] = terrain.mesh_surface_points[i + 1];
                point.position[2] = terrain.mesh_surface_points[i + 2];
                data.points.push_back(point);
            }
            data.indices = terrain.mesh_surface_indices;
            build_triangle_bounds_and_grid(data);
            data.min_height = terrain.min_height;
            data.max_height = terrain.max_height;
            data.supports_ray_query = terrain.supports_ray_query;
            data.supports_height_query = terrain.supports_height_query;
            data.supports_overlap_query = !data.indices.empty();
            return data;
        }
    }

    void register_collision_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        TerrainAssetTable& terrain_table,
        CollisionAssetTable& collision_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kCollisionFromMeshSchema,
            .output_type = kAssetTypeCollisionAsset,
            .compile = [&logger, &mesh_table, &collision_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<CollisionFromMeshCompileDesc>(&input.meta);
                if (!desc) {
                    logger.error("collision mesh missing compile desc");
                    return compile_failed_node(input);
                }
                if (dep_handles.size() != 1) {
                    logger.error("collision mesh requires one mesh dependency");
                    return compile_failed_node(input);
                }

                const MeshData* mesh = mesh_table.get(dep_handles[0]);
                if (!mesh || !mesh->valid()) {
                    logger.error("collision mesh source is invalid");
                    return compile_failed_node(input);
                }

                CollisionAssetData data = collision_from_mesh(*desc, *mesh);
                if (!data.valid()) {
                    logger.error("compiled mesh collision asset is invalid");
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle =
                    collision_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store mesh collision asset");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kCollisionFromTerrainSchema,
            .output_type = kAssetTypeCollisionAsset,
            .compile = [&logger, &terrain_table, &collision_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<CollisionFromTerrainCompileDesc>(
                        &input.meta);
                if (!desc) {
                    logger.error("collision terrain missing compile desc");
                    return compile_failed_node(input);
                }
                if (dep_handles.size() != 1) {
                    logger.error(
                        "collision terrain requires one terrain dependency");
                    return compile_failed_node(input);
                }

                const TerrainAssetData* terrain =
                    terrain_table.get(dep_handles[0]);
                if (!terrain || !terrain->valid()) {
                    logger.error("collision terrain source is invalid");
                    return compile_failed_node(input);
                }

                CollisionAssetData data =
                    collision_from_terrain(*desc, *terrain);
                if (!data.valid()) {
                    logger.error("compiled terrain collision asset is invalid");
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle =
                    collision_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store terrain collision asset");
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
