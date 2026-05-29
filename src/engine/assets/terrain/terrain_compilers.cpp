// src/engine/assets/terrain/terrain_compilers.cpp

#include <engine/assets/terrain/terrain_compilers.h>

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
                copy_mesh_bounds(data.bounds_min, data.bounds_max, *mesh);
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
