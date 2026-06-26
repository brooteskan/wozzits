// src/engine/assets/engine_asset_library_compiler_registry.cpp

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/type_extensions.h>

#include <engine/assets/shader/shader_compilers.h>
#include <engine/assets/scalar_field/scalar_field_compilers.h>
#include <engine/assets/csv/csv_compilers.h>
#include <engine/assets/json/json_compilers.h>
#include <engine/assets/toml/toml_compilers.h>
#include <engine/assets/mesh/mesh_compilers.h>
#include <engine/assets/diagnostics/diagnostic_timeframe_summary_compilers.h>

namespace wz::engine::assets::internal
{
    wz::asset::CompilerRegistry make_engine_compiler_registry(const EngineAssetContext& ctx)
    {
        validate_asset_type_display_names();

        wz::asset::CompilerRegistry registry;

        register_file_carrier_compilers(registry, ctx.logger);
        register_shader_compilers(
            registry, ctx.logger, ctx.device, ctx.shader_module_registry);
        register_scalar_field_compilers(
            registry,
            ctx.logger,
            ctx.scalar_fields_table,
            ctx.cache_settings,
            ctx.gpu_resources,
            ctx.rhi_resource_tracker);
        register_vector_field_compilers(registry, ctx.logger, ctx.vector_fields_table);
        register_csv_compilers(registry, ctx.logger, ctx.csv_table);
        register_json_compilers(registry, ctx.logger, ctx.json_table);
        register_toml_compilers(registry, ctx.logger, ctx.toml_table);
        register_mesh_compilers(
            registry,
            ctx.logger,
            ctx.mesh_table,
            ctx.scalar_fields_table,
            ctx.cache_settings);
        register_mesh_derived_field_compilers(
            registry,
            ctx.logger,
            ctx.mesh_field_compute,
            ctx.mesh_table,
            ctx.compute_pipeline_table,
            ctx.mesh_derived_field_table,
            ctx.mesh_sparse_operator_table,
            ctx.gpu_resident_field_table,
            ctx.gpu_resident_mesh_data_table,
            ctx.cache_settings);
        register_mesh_sparse_operator_compilers(
            registry,
            ctx.logger,
            ctx.mesh_table,
            ctx.mesh_sparse_operator_table,
            ctx.cache_settings);
        register_gpu_sparse_mesh_compilers(
            registry,
            ctx.logger,
            ctx.mesh_field_compute,
            ctx.mesh_table,
            ctx.gpu_sparse_mesh_table,
            ctx.gpu_resident_sparse_mesh_table,
            ctx.gpu_resources,
            ctx.rhi_resource_tracker);
        register_mesh_cluster_hierarchy_compilers(
            registry,
            ctx.logger,
            ctx.mesh_field_compute,
            ctx.mesh_table,
            ctx.compute_pipeline_table,
            ctx.mesh_derived_field_table,
            ctx.mesh_sparse_operator_table,
            ctx.gpu_resident_mesh_data_table,
            ctx.gpu_resident_sparse_operator_table,
            ctx.gpu_resident_mesh_cluster_hierarchy_table,
            ctx.mesh_cluster_hierarchy_table);
        register_terrain_compilers(registry, ctx.logger, ctx.scalar_fields_table, ctx.mesh_table, ctx.terrain_table, ctx.cache_settings);
        register_terrain_visual_proxy_compilers(registry, ctx.logger, ctx.terrain_table, ctx.terrain_visual_proxy_table, ctx.cache_settings);
        register_collision_compilers(registry, ctx.logger, ctx.mesh_table, ctx.terrain_table, ctx.collision_table, ctx.cache_settings);
        register_gaussian_splat_compilers(
            registry,
            ctx.logger,
            ctx.gaussian_splat_cloud_table,
            ctx.scalar_fields_table,
            ctx.gpu_resources,
            ctx.rhi_resource_tracker);
        register_gaussian_splat_terrain_surface_compiler(registry, ctx.logger, ctx.gaussian_splat_cloud_table, ctx.scalar_fields_table);
        register_terrain_splat_from_gaea_r32_compiler(registry, ctx.logger, ctx.gaussian_splat_cloud_table, ctx.json_table);
        register_gaussian_splat_color_lod_compiler(registry, ctx.logger, ctx.gaussian_splat_cloud_table, ctx.gaussian_splat_color_lod_table);
        register_data_table_compilers(registry, ctx.logger, ctx.data_table);
        register_diagnostic_resampled_time_series_compilers(registry, ctx.logger, ctx.data_table, ctx.diagnostic_resampled_time_series_table);
        register_diagnostic_timeframe_summary_compilers(registry, ctx.logger, ctx.data_table, ctx.diagnostic_timeframe_summary_table);
        register_csv_export_compilers(registry, ctx.logger, ctx.data_table, ctx.csv_export_table);
        register_mesh_render_style_compilers(registry, ctx.logger, ctx.mesh_render_style_table);
        register_renderable_compilers(registry, ctx);
        register_render_program_compilers(
            registry,
            ctx.logger,
            ctx.render_program_table,
            ctx.render_program_registry,
            ctx.descriptor_semantic_registry,
            ctx.constant_semantic_registry);
        register_compute_pipeline_compilers(registry, ctx.logger, ctx.compute_pipeline_table);
        register_light_compilers(
            registry,
            ctx.logger,
            ctx.direct_light_table,
            ctx.ambient_lighting_table,
            ctx.hdri_environment_table);
        register_scene_compilers(
            registry,
            ctx.logger,
            ctx.json_table,
            ctx.scene_table,
            ctx.mesh_table);

        return registry;
    }

} // namespace wz::engine::assets::internal
