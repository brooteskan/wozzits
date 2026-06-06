#pragma once

// engine/assets/engine_asset_library.h

#include <asset/system.h>
#include <asset/compiler.h>
#include <asset/types.h>

#include <file/filesystem.h>

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>
#include <gpu/shader.h>

#include <logging/logger.h>

#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/vector_field/vector_field.h>
#include <engine/assets/csv/csv.h>
#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/vector_field_asset_module.h>
#include <engine/assets/csv_asset_module.h>
#include <engine/assets/asset_cache_settings.h>

#include <engine/assets/toml/toml.h>
#include <engine/assets/toml_asset_module.h>

#include <engine/assets/json/json.h>
#include <engine/assets/json_asset_module.h>

#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_asset_module.h>

#include <engine/assets/terrain/terrain.h>
#include <engine/assets/terrain_asset_module.h>

#include <engine/assets/collision/collision.h>
#include <engine/assets/collision_asset_module.h>

#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>
#include <engine/assets/gaussian_splat_color_lod_asset_module.h>

#include <engine/assets/data_table_asset_module.h>
#include <engine/assets/diagnostic_resampled_time_series_asset_module.h>
#include <engine/assets/diagnostic_timeframe_summary_asset_module.h>
#include <engine/assets/csv_export_asset_module.h>

#include <engine/assets/mesh_render_style_asset_module.h>
#include <engine/assets/renderable_asset_module.h>

#include <engine/assets/render_program/render_program_asset_module.h>

#include <engine/assets/light/light.h>
#include <engine/assets/light_asset_module.h>

#include <engine/assets/scene/scene.h>
#include <engine/assets/scene_asset_module.h>

#include <string>
#include <vector>

namespace wz::engine::assets
{
    // ─── ResolveReport ────────────────────────────────────────────────────────────
    //
    // Returned by EngineAssetLibrary::resolve_all(). Carries the success count
    // and a structured list of failures for diagnostic use.

    struct ResolveFailure
    {
        wz::asset::AssetKey     key;
        wz::asset::ResolveError error;
    };

    struct ResolveReport
    {
        uint32_t                  resolved_count = 0;
        std::vector<ResolveFailure> failures;

        bool ok() const noexcept { return failures.empty(); }
    };


    // ─── EngineAssetLibrary ───────────────────────────────────────────────────────

    class EngineAssetLibrary
    {
    public:
        EngineAssetLibrary(
            wz::gpu::Device& device,
            wz::Logger& logger,
            wz::fs::Path      resource_root
        );

        EngineAssetLibrary(
            wz::gpu::Device& device,
            wz::Logger& logger,
            wz::fs::Path      resource_root,
            EngineAssetCacheSettings cache_settings
        );

        EngineAssetLibrary(const EngineAssetLibrary&) = delete;
        EngineAssetLibrary& operator=(const EngineAssetLibrary&) = delete;

        EngineAssetLibrary(EngineAssetLibrary&&) = delete;
        EngineAssetLibrary& operator=(EngineAssetLibrary&&) = delete;

        // ── Graph lifecycle ───────────────────────────────────────────────────────

        bool          commit();
        ResolveReport resolve_all();

        const wz::fs::Path& resource_root() const noexcept
        {
            return resource_root_;
        }

        const EngineAssetCacheSettings& cache_settings() const noexcept
        {
            return cache_settings_;
        }

        const wz::fs::Path& cache_root() const noexcept
        {
            return cache_settings_.root;
        }

        // ── Module accessors ──────────────────────────────────────────────────────

        FileCarrierAssetModule&       files()         { return files_; }
        ShaderAssetModule&            shaders()       { return shaders_; }
        ScalarFieldAssetModule&       scalar_fields() { return scalar_fields_; }
        VectorFieldAssetModule&       vector_fields() { return vector_fields_; }
        CSVAssetModule&               csv()           { return csv_; }

        const FileCarrierAssetModule&  files()         const { return files_; }
        const ShaderAssetModule&       shaders()       const { return shaders_; }
        const ScalarFieldAssetModule&  scalar_fields() const { return scalar_fields_; }
        const VectorFieldAssetModule&  vector_fields() const { return vector_fields_; }
        const CSVAssetModule&          csv()           const { return csv_; }

        JSONAssetModule&               json()           { return json_; }
        const JSONAssetModule&         json()     const { return json_; }

        TOMLAssetModule&               toml()           { return toml_; }
        const TOMLAssetModule&         toml()     const { return toml_; }

        MeshAssetModule&               meshes()         { return meshes_; }
        const MeshAssetModule&         meshes()   const { return meshes_; }

        TerrainAssetModule&            terrains()       { return terrains_; }
        const TerrainAssetModule&      terrains() const { return terrains_; }

        CollisionAssetModule&       collisions()       { return collisions_; }
        const CollisionAssetModule& collisions() const { return collisions_; }

        GaussianSplatAssetModule&       gaussian_splats()         { return gaussian_splats_; }
        const GaussianSplatAssetModule& gaussian_splats()   const { return gaussian_splats_; }

        GaussianSplatColorLODAssetModule&       gaussian_splat_color_lods()       { return gaussian_splat_color_lods_; }
        const GaussianSplatColorLODAssetModule& gaussian_splat_color_lods() const { return gaussian_splat_color_lods_; }

        DataTableAssetModule&       data_tables()       { return data_tables_; }
        const DataTableAssetModule& data_tables() const { return data_tables_; }

        DiagnosticResampledTimeSeriesAssetModule&       diagnostic_resampled_time_series()       { return diagnostic_resampled_time_series_;  }
        const DiagnosticResampledTimeSeriesAssetModule& diagnostic_resampled_time_series() const { return diagnostic_resampled_time_series_; }

        DiagnosticTimeframeSummaryAssetModule&       diagnostic_timeframe_summaries()       { return diagnostic_timeframe_summaries_; }
        const DiagnosticTimeframeSummaryAssetModule& diagnostic_timeframe_summaries() const { return diagnostic_timeframe_summaries_; }

        CSVExportAssetModule&       csv_export()       { return csv_export_; }
        const CSVExportAssetModule& csv_export() const { return csv_export_; }

        MeshRenderStyleAssetModule&       mesh_render_styles()       { return mesh_render_styles_; }
        const MeshRenderStyleAssetModule& mesh_render_styles() const { return mesh_render_styles_; }

        RenderableAssetModule&       renderables()        { return renderables_; }
        const RenderableAssetModule& renderables()  const { return renderables_; }

        RenderProgramAssetModule&       render_programs()       { return render_programs_; }
        const RenderProgramAssetModule& render_programs() const { return render_programs_; }

        LightAssetModule&       lights()       { return lights_; }
        const LightAssetModule& lights() const { return lights_; }

        SceneAssetModule&       scenes()       { return scenes_; }
        const SceneAssetModule& scenes() const { return scenes_; }

        // ── Direct access ─────────────────────────────────────────────────────────

        wz::asset::AssetSystem&       system()       { return system_; }
        const wz::asset::AssetSystem& system() const { return system_; }

    private:
        // Member declaration order is load-bearing — C++ initialises in this order.
        //
        // *_table_ members before system_: compiler registry lambdas capture
        //   references to the tables; they must be alive when system_ is constructed.
        //
        // system_ before the modules: modules hold a reference to system_.
        //
        // files_ before shaders_, scalar_fields_, and csv_: all three modules
        //   hold a reference to files_.

        wz::gpu::Device& device_;
        wz::Logger&      logger_;
        wz::fs::Path     resource_root_;
        EngineAssetCacheSettings cache_settings_;

        ScalarFieldTable            scalar_fields_table_;
        VectorFieldTable            vector_fields_table_;
        CSVTable                    csv_table_;
        JSONTable                   json_table_;
        TOMLTable                   toml_table_;
        MeshTable                   mesh_table_;
        TerrainAssetTable           terrain_table_;
        CollisionAssetTable         collision_table_;
        GaussianSplatCloudTable     gaussian_splat_cloud_table_;
        GaussianSplatColorLODTable  gaussian_splat_color_lod_table_;
        DataTable                   data_table_;
        DiagnosticResampledTimeSeriesTable  diagnostic_resampled_time_series_table_;
        DiagnosticTimeframeSummaryTable     diagnostic_timeframe_summary_table_;
        CSVExportTable                      csv_export_table_;
        MeshRenderStyleTable        mesh_render_style_table_;
        RenderableAssetTable        renderable_table_;
        RenderProgramTable          render_program_table_;
        DirectLightTable            direct_light_table_;
        AmbientLightingTable        ambient_lighting_table_;
        HDRIEnvironmentTable        hdri_environment_table_;
        SceneAssetTable             scene_table_;

        wz::asset::AssetSystem system_;

        FileCarrierAssetModule      files_;
        ShaderAssetModule           shaders_;
        ScalarFieldAssetModule      scalar_fields_;
        VectorFieldAssetModule      vector_fields_;
        CSVAssetModule              csv_;
        JSONAssetModule             json_;
        TOMLAssetModule             toml_;
        MeshAssetModule             meshes_;
        TerrainAssetModule          terrains_;
        CollisionAssetModule        collisions_;
        GaussianSplatAssetModule    gaussian_splats_;
        GaussianSplatColorLODAssetModule gaussian_splat_color_lods_;
        DataTableAssetModule        data_tables_;
        DiagnosticResampledTimeSeriesAssetModule diagnostic_resampled_time_series_;
        DiagnosticTimeframeSummaryAssetModule    diagnostic_timeframe_summaries_;
        CSVExportAssetModule                     csv_export_;
        MeshRenderStyleAssetModule  mesh_render_styles_;
        RenderableAssetModule       renderables_;
        RenderProgramAssetModule    render_programs_;
        LightAssetModule            lights_;
        SceneAssetModule            scenes_;
    };

} // namespace wz::engine::assets
