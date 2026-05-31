// src/engine/assets/collision/collision_compilers.cpp

#include <engine/assets/collision/collision_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <span>

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
            data.min_height = terrain.min_height;
            data.max_height = terrain.max_height;
            data.supports_ray_query = terrain.supports_ray_query;
            data.supports_height_query = terrain.supports_height_query;
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
