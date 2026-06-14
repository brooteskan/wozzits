#pragma once

#include <file/filesystem.h>
#include <asset/types.h>
#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/vector_field/vector_field.h>
#include <engine/assets/vector_field/vector_field_compilers.h>
#include <engine/assets/csv/csv.h>
#include <engine/assets/json/json.h>
#include <engine/assets/toml/toml.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field_compilers.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator_compilers.h>
#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy.h>
#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.h>
#include <engine/assets/terrain/terrain.h>
#include <engine/assets/terrain/terrain_compilers.h>
#include <engine/assets/terrain/terrain_visual_proxy.h>
#include <engine/assets/terrain/terrain_visual_proxy_compilers.h>
#include <engine/assets/collision/collision.h>
#include <engine/assets/collision/collision_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod_compiler.h>
#include <engine/assets/data_table/data_table.h>
#include <engine/assets/data_table/data_table_compilers.h>
#include <engine/assets/diagnostics/diagnostic_resampled_time_series.h>
#include <engine/assets/diagnostics/diagnostic_resampled_time_series_compilers.h>
#include <engine/assets/diagnostics/diagnostic_timeframe_summary.h>
#include <engine/assets/diagnostics/diagnostic_timeframe_summary_compilers.h>
#include <engine/assets/csv_export/csv_export.h>
#include <engine/assets/csv_export/csv_export_compilers.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable/renderable_compilers.h>
#include <engine/assets/mesh_render_style/mesh_render_style.h>
#include <engine/assets/mesh_render_style/mesh_render_style_compilers.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/render_program/render_program_compilers.h>
#include <engine/assets/compute_pipeline/compute_pipeline.h>
#include <engine/assets/compute_pipeline/compute_pipeline_compilers.h>
#include <engine/assets/light/light.h>
#include <engine/assets/light/light_compilers.h>
#include <engine/assets/scene/scene.h>
#include <engine/assets/scene/scene_compilers.h>

#include <gpu/shader.h>

namespace wz::engine::assets::internal {

    struct FileSourceDesc
    {
        wz::fs::Path full_path;
        std::string  canonical_path;
    };

    struct EngineAssetContext
    {
        wz::gpu::Device&            device;
        wz::Logger&                 logger;
        MeshFieldComputeBackend&    mesh_field_compute;
        ScalarFieldTable&           scalar_fields_table;
        VectorFieldTable&           vector_fields_table;
        CSVTable&                   csv_table;
        JSONTable&                  json_table;
        TOMLTable&                  toml_table;
        MeshTable&                  mesh_table;
        MeshDerivedFieldTable&      mesh_derived_field_table;
        MeshSparseOperatorTable&    mesh_sparse_operator_table;
        MeshClusterHierarchyTable&  mesh_cluster_hierarchy_table;
        GpuResidentFieldTable&      gpu_resident_field_table;
        GpuResidentMeshDataTable&   gpu_resident_mesh_data_table;
        GpuResidentSparseOperatorTable& gpu_resident_sparse_operator_table;
        GpuResidentMeshClusterHierarchyTable&
            gpu_resident_mesh_cluster_hierarchy_table;
        TerrainAssetTable&          terrain_table;
        TerrainVisualProxyTable&     terrain_visual_proxy_table;
        CollisionAssetTable&        collision_table;
        GaussianSplatCloudTable&    gaussian_splat_cloud_table;
        GaussianSplatColorLODTable& gaussian_splat_color_lod_table;
        DataTable&                          data_table;
        DiagnosticResampledTimeSeriesTable& diagnostic_resampled_time_series_table;
        DiagnosticTimeframeSummaryTable&    diagnostic_timeframe_summary_table;
        CSVExportTable&                     csv_export_table;
        MeshRenderStyleTable&               mesh_render_style_table;
        RenderableAssetTable&               renderable_table;
        RenderProgramTable&                 render_program_table;
        ComputePipelineTable&               compute_pipeline_table;
        DirectLightTable&                   direct_light_table;
        AmbientLightingTable&               ambient_lighting_table;
        HDRIEnvironmentTable&               hdri_environment_table;
        SceneAssetTable&                    scene_table;
        EngineAssetCacheSettings            cache_settings;
    };

    wz::asset::AssetNode compile_failed_node(
        const wz::asset::AssetNode& input
    );

    wz::asset::CompilerRegistry make_engine_compiler_registry(
        const EngineAssetContext& ctx
    );

    void register_file_carrier_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger
    );

}
