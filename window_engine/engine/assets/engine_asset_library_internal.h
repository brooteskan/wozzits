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
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>
#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy.h>
#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.h>
#include <engine/assets/terrain/terrain.h>
#include <engine/assets/terrain/terrain_compilers.h>
#include <engine/assets/terrain/terrain_visual_proxy.h>
#include <engine/assets/terrain/terrain_visual_proxy_compilers.h>
#include <engine/assets/collision/collision.h>
#include <engine/assets/collision/collision_compilers.h>
#include <engine/assets/placement/placement.h>
#include <engine/assets/placement/placement_compilers.h>
#include <engine/assets/placed_field/placed_field.h>
#include <engine/assets/placed_field/placed_field_compilers.h>
#include <engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule.h>
#include <engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule_compilers.h>
#include <engine/assets/atmosphere/atmosphere.h>
#include <engine/assets/atmosphere/atmosphere_compilers.h>
#include <engine/assets/audio/audio_clip.h>
#include <engine/assets/audio/audio_clip_compilers.h>
#include <engine/assets/audio/audio_clip_bank.h>
#include <engine/assets/audio/audio_clip_bank_compilers.h>
#include <engine/assets/audio/audio_renderable.h>
#include <engine/assets/audio/audio_renderable_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod_compiler.h>
#include <engine/assets/sky_gaussian/sky_gaussian_table.h>
#include <engine/assets/sky_gaussian/sky_gaussian_compilers.h>
#include <engine/assets/star_catalog/star_catalog_table.h>
#include <engine/assets/star_catalog/star_catalog_compilers.h>
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
#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/render_binding_layout/render_binding_layout_compilers.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/render_program/render_program_compilers.h>
#include <engine/assets/compute_pipeline/compute_pipeline.h>
#include <engine/assets/compute_pipeline/compute_pipeline_compilers.h>
#include <engine/assets/light/light.h>
#include <engine/assets/light/light_compilers.h>
#include <engine/assets/scene/scene.h>
#include <engine/assets/scene/scene_compilers.h>

#include <gpu/shader.h>

#include <wozzits/rhi/constants_layout.h>
#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/render_program_registry.h>
#include <wozzits/rhi/shader_module.h>
#include <wozzits/rhi/shader_resource_group_layout.h>

#include <functional>
#include <string>
#include <vector>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::asset
{
    class AssetSystem;
}

namespace wz::engine::assets::internal {

    struct FileSourceDesc
    {
        wz::fs::Path full_path;
        std::string  canonical_path;
    };

    // RhiResourceTracker (asset-type agnostic residency-record hook) is defined
    // in gpu_sparse_mesh_compilers.h, included above.

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
        GpuSparseMeshTable&         gpu_sparse_mesh_table;
        MeshClusterHierarchyTable&  mesh_cluster_hierarchy_table;
        GpuResidentFieldTable&      gpu_resident_field_table;
        GpuResidentMeshDataTable&   gpu_resident_mesh_data_table;
        GpuResidentSparseOperatorTable& gpu_resident_sparse_operator_table;
        GpuResidentSparseMeshTable& gpu_resident_sparse_mesh_table;
        GpuResidentMeshClusterHierarchyTable&
            gpu_resident_mesh_cluster_hierarchy_table;
        TerrainAssetTable&          terrain_table;
        TerrainVisualProxyTable&     terrain_visual_proxy_table;
        CollisionAssetTable&        collision_table;
        PlacementTable&             placement_table;
        PlacedFieldTable&           placed_field_table;
        ClipmapLatticeScheduleTable& clipmap_lattice_schedule_table;
        AtmosphereTable&            atmosphere_table;
        AudioClipTable&             audio_clip_table;
        AudioClipBankTable&         audio_clip_bank_table;
        AudioRenderableTable&       audio_renderable_table;
        GaussianSplatCloudTable&    gaussian_splat_cloud_table;
        GaussianSplatColorLODTable& gaussian_splat_color_lod_table;
        DataTable&                          data_table;
        DiagnosticResampledTimeSeriesTable& diagnostic_resampled_time_series_table;
        DiagnosticTimeframeSummaryTable&    diagnostic_timeframe_summary_table;
        CSVExportTable&                     csv_export_table;
        MeshRenderStyleTable&               mesh_render_style_table;
        RenderBindingLayoutTable&           render_binding_layout_table;
        RenderableAssetTable&               renderable_table;
        RhiRenderableTable&                 rhi_renderable_table;
        RenderProgramTable&                 render_program_table;
        ComputePipelineTable&               compute_pipeline_table;
        DirectLightTable&                   direct_light_table;
        AmbientLightingTable&               ambient_lighting_table;
        HDRIEnvironmentTable&               hdri_environment_table;
        SkyGaussianTable&                   sky_gaussian_table;
        wz::engine::starfield::StarCatalogTable& star_catalog_table;
        SceneAssetTable&                    scene_table;
        EngineAssetCacheSettings            cache_settings;
        wz::rhi::GpuResourceRegistry*       gpu_resources = nullptr;
        // Generic, asset-type agnostic residency tracker hook. Compilers that
        // acquire shared-registry buffers record (key → identities) through this
        // so the library can release them on de-registration. May be empty.
        RhiResourceTracker                  rhi_resource_tracker;
        // Shared rhi registries (owned by EngineGpuContext): the shader compiler
        // registers ShaderModules into shader_module_registry, the render-program
        // compiler registers the program into render_program_registry (resolving
        // its semantics through the descriptor/constant registries). Null for a
        // device-only library, where the compilers skip rhi production and the
        // renderer falls back to bridging the legacy program at render time.
        wz::rhi::RenderProgramRegistry*      render_program_registry = nullptr;
        wz::rhi::ShaderModuleRegistry*       shader_module_registry = nullptr;
        wz::rhi::DescriptorSemanticRegistry* descriptor_semantic_registry = nullptr;
        wz::rhi::ConstantSemanticRegistry*   constant_semantic_registry = nullptr;
        // The owning AssetSystem (the library's system_). Compilers only ever
        // DEREFERENCE it inside compile fns — during resolve, long after
        // construction — so the library passes &system_ while system_ is still
        // being constructed. Compilers whose ports cannot be told apart by
        // dependency type (the 0x70A custom renderable's Any-typed binding
        // ports) read the input node's PORT-ORDERED dep keys (empty key =
        // unwired optional port) from asset_system->registered_assets(); that
        // list is part of the node's identity (its key folds the dep
        // composition), so reading it keeps compiles content-addressed. Null
        // for schema-only registries (asset_graph_schema_registry) that never
        // invoke compile.
        const wz::asset::AssetSystem*        asset_system = nullptr;
    };

    wz::asset::AssetNode compile_failed_node(
        const wz::asset::AssetNode& input
    );

    // Overload carrying a human-readable failure reason. The reason rides back on
    // the returned node's error_detail; resolve() lifts it into NodeResolveState
    // and the ResolveReport so the editor can surface it on the failing node.
    wz::asset::AssetNode compile_failed_node(
        const wz::asset::AssetNode& input,
        std::string reason
    );

    wz::asset::CompilerRegistry make_engine_compiler_registry(
        const EngineAssetContext& ctx
    );

    void register_file_carrier_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger
    );

}
