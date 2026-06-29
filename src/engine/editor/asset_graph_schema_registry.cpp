#include <engine/editor/asset_graph_schema_registry.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/mesh_derived_field/mesh_field_compute.h>

#include <gpu/gpu.h>
#include <logging/logger.h>

#include <memory>
#include <utility>

namespace wz::engine::editor
{
    namespace
    {
        using namespace wz::engine::assets;
        using namespace wz::engine::assets::internal;

        // Device-free stand-ins for every table EngineAssetContext references.
        // Constructing these touches no GPU: the resident tables are plain
        // containers, and the compute backend only stores the device reference
        // (deferring all GPU work to method calls the schema path never makes).
        // The device's impl is null; it is never used.
        struct SchemaRegistryStubs
        {
            wz::gpu::Device device{};
            wz::Logger      logger{};
            std::unique_ptr<MeshFieldComputeBackend> mesh_field_compute =
                make_gpu_mesh_field_compute_backend(device);

            ScalarFieldTable            scalar_fields_table;
            VectorFieldTable            vector_fields_table;
            CSVTable                    csv_table;
            JSONTable                   json_table;
            TOMLTable                   toml_table;
            MeshTable                   mesh_table;
            MeshDerivedFieldTable       mesh_derived_field_table;
            MeshSparseOperatorTable     mesh_sparse_operator_table;
            GpuSparseMeshTable          gpu_sparse_mesh_table;
            MeshClusterHierarchyTable   mesh_cluster_hierarchy_table;
            GpuResidentFieldTable       gpu_resident_field_table;
            GpuResidentMeshDataTable    gpu_resident_mesh_data_table;
            GpuResidentSparseOperatorTable gpu_resident_sparse_operator_table;
            GpuResidentSparseMeshTable  gpu_resident_sparse_mesh_table;
            GpuResidentMeshClusterHierarchyTable
                gpu_resident_mesh_cluster_hierarchy_table;
            TerrainAssetTable           terrain_table;
            TerrainVisualProxyTable     terrain_visual_proxy_table;
            CollisionAssetTable         collision_table;
            PlacementTable              placement_table;
            AudioClipTable              audio_clip_table;
            GaussianSplatCloudTable     gaussian_splat_cloud_table;
            GaussianSplatColorLODTable  gaussian_splat_color_lod_table;
            DataTable                   data_table;
            DiagnosticResampledTimeSeriesTable diagnostic_resampled_time_series_table;
            DiagnosticTimeframeSummaryTable    diagnostic_timeframe_summary_table;
            CSVExportTable              csv_export_table;
            MeshRenderStyleTable        mesh_render_style_table;
            RenderableAssetTable        renderable_table;
            RhiRenderableTable          rhi_renderable_table;
            RenderProgramTable          render_program_table;
            ComputePipelineTable        compute_pipeline_table;
            DirectLightTable            direct_light_table;
            AmbientLightingTable        ambient_lighting_table;
            HDRIEnvironmentTable        hdri_environment_table;
            SceneAssetTable             scene_table;
            EngineAssetCacheSettings    cache_settings{};

            EngineAssetContext context()
            {
                return EngineAssetContext{
                    .device                    = device,
                    .logger                    = logger,
                    .mesh_field_compute        = *mesh_field_compute,
                    .scalar_fields_table       = scalar_fields_table,
                    .vector_fields_table       = vector_fields_table,
                    .csv_table                 = csv_table,
                    .json_table                = json_table,
                    .toml_table                = toml_table,
                    .mesh_table                = mesh_table,
                    .mesh_derived_field_table  = mesh_derived_field_table,
                    .mesh_sparse_operator_table = mesh_sparse_operator_table,
                    .gpu_sparse_mesh_table     = gpu_sparse_mesh_table,
                    .mesh_cluster_hierarchy_table = mesh_cluster_hierarchy_table,
                    .gpu_resident_field_table  = gpu_resident_field_table,
                    .gpu_resident_mesh_data_table = gpu_resident_mesh_data_table,
                    .gpu_resident_sparse_operator_table =
                        gpu_resident_sparse_operator_table,
                    .gpu_resident_sparse_mesh_table =
                        gpu_resident_sparse_mesh_table,
                    .gpu_resident_mesh_cluster_hierarchy_table =
                        gpu_resident_mesh_cluster_hierarchy_table,
                    .terrain_table             = terrain_table,
                    .terrain_visual_proxy_table = terrain_visual_proxy_table,
                    .collision_table           = collision_table,
                    .placement_table           = placement_table,
                    .audio_clip_table          = audio_clip_table,
                    .gaussian_splat_cloud_table = gaussian_splat_cloud_table,
                    .gaussian_splat_color_lod_table =
                        gaussian_splat_color_lod_table,
                    .data_table                = data_table,
                    .diagnostic_resampled_time_series_table =
                        diagnostic_resampled_time_series_table,
                    .diagnostic_timeframe_summary_table =
                        diagnostic_timeframe_summary_table,
                    .csv_export_table          = csv_export_table,
                    .mesh_render_style_table   = mesh_render_style_table,
                    .renderable_table          = renderable_table,
                    .rhi_renderable_table      = rhi_renderable_table,
                    .render_program_table      = render_program_table,
                    .compute_pipeline_table    = compute_pipeline_table,
                    .direct_light_table        = direct_light_table,
                    .ambient_lighting_table    = ambient_lighting_table,
                    .hdri_environment_table    = hdri_environment_table,
                    .scene_table               = scene_table,
                    .cache_settings            = cache_settings,
                };
            }
        };
    }

    wz::asset::CompilerRegistry build_asset_graph_schema_registry()
    {
        SchemaRegistryStubs stubs;
        const wz::asset::CompilerRegistry full =
            make_engine_compiler_registry(stubs.context());

        // The full registry's compile fns captured `stubs` by reference and
        // would dangle once `stubs` is destroyed. The editor session reads
        // schema only (input_ports/types) and never invokes compile, so we copy
        // the schema fields into a self-contained registry with null compile
        // fns and let the stubs go.
        wz::asset::CompilerRegistry schema;
        for (const auto& [key, compiler] : full.compilers()) {
            (void)key;
            schema.register_compiler(wz::asset::AssetCompiler{
                .input_schema = compiler.input_schema,
                .output_type  = compiler.output_type,
                .input_ports  = compiler.input_ports,
                .parameters   = compiler.parameters,
                .compile      = nullptr,
            });
        }
        return schema;
    }
}
