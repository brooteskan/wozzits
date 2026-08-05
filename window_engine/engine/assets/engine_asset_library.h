#pragma once

// engine/assets/engine_asset_library.h

#include <asset/system.h>
#include <asset/compiler.h>
#include <asset/draft.h>
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
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_derived_field/mesh_field_compute.h>
#include <engine/assets/mesh_derived_field_asset_module.h>

#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>
#include <engine/assets/mesh_sparse_operator_asset_module.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh.h>
#include <engine/assets/gpu_sparse_mesh_asset_module.h>
#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy.h>
#include <engine/assets/mesh_cluster_hierarchy_asset_module.h>

#include <engine/assets/terrain/terrain.h>
#include <engine/assets/terrain_asset_module.h>
#include <engine/assets/terrain/terrain_visual_proxy.h>
#include <engine/assets/terrain_visual_proxy_asset_module.h>

#include <engine/assets/collision/collision.h>
#include <engine/assets/collision_asset_module.h>

#include <engine/assets/placement/placement.h>
#include <engine/assets/placement_asset_module.h>
#include <engine/assets/placed_field/placed_field.h>
#include <engine/assets/placed_field_asset_module.h>
#include <engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule.h>
#include <engine/assets/clipmap_lattice_schedule_asset_module.h>
#include <engine/assets/atmosphere/atmosphere.h>
#include <engine/assets/atmosphere_asset_module.h>
#include <engine/assets/environment/environment.h>
#include <engine/assets/environment_asset_module.h>

#include <engine/assets/audio/audio_clip.h>
#include <engine/assets/audio_clip_asset_module.h>
#include <engine/assets/audio/audio_clip_bank.h>
#include <engine/assets/audio_clip_bank_asset_module.h>
#include <engine/assets/audio/audio_renderable.h>
#include <engine/assets/audio_renderable_asset_module.h>

#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>
#include <engine/assets/gaussian_splat_color_lod_asset_module.h>
#include <engine/assets/inochi/puppet_table.h>
#include <engine/assets/puppet_asset_module.h>

#include <engine/assets/sky_gaussian/sky_gaussian_table.h>
#include <engine/assets/sky_gaussian_asset_module.h>
#include <engine/assets/star_catalog/star_catalog_table.h>
#include <engine/assets/star_catalog_asset_module.h>
#include <engine/assets/texture/texture.h>
#include <engine/assets/texture_asset_module.h>

#include <engine/assets/data_table_asset_module.h>
#include <engine/assets/diagnostic_resampled_time_series_asset_module.h>
#include <engine/assets/diagnostic_timeframe_summary_asset_module.h>
#include <engine/assets/csv_export_asset_module.h>

#include <engine/assets/mesh_render_style_asset_module.h>
#include <engine/assets/render_binding_layout_asset_module.h>
#include <engine/assets/renderable_asset_module.h>

#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/compute_pipeline_asset_module.h>

#include <engine/assets/light/light.h>
#include <engine/assets/light_asset_module.h>

#include <engine/assets/scene/scene.h>
#include <engine/assets/scene_asset_module.h>

#include <engine/assets/authoring/asset_graph_authoring.h>

#include <wozzits/rhi/gpu_resource.h>

#include <source_location>
#include <string>
#include <vector>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::rendering
{
    struct EngineGpuContext;
}

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
        // Human-readable failure reason (empty when the compiler supplied none),
        // captured from the failing node's resolve state.
        std::string             detail;
    };

    struct ResolveReport
    {
        uint32_t                  resolved_count = 0;
        uint32_t                  evicted_count = 0;
        std::vector<ResolveFailure> failures;

        bool ok() const noexcept { return failures.empty(); }
    };


    // ─── EngineAssetLibrary ───────────────────────────────────────────────────────

    class EngineAssetLibrary
    {
    public:
        EngineAssetLibrary(
            wz::engine::rendering::EngineGpuContext& gpu,
            wz::Logger& logger,
            wz::fs::Path      resource_root
        );

        EngineAssetLibrary(
            wz::engine::rendering::EngineGpuContext& gpu,
            wz::Logger& logger,
            wz::fs::Path      resource_root,
            EngineAssetCacheSettings cache_settings
        );

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

        ~EngineAssetLibrary();

        // ── Graph lifecycle ───────────────────────────────────────────────────────

        bool          commit();
        struct AssetGraphDraftCommitReport
        {
            enum class Status
            {
                Success,
                MaterializeFailed,
                ReplaceFailed,
            };

            Status status = Status::Success;
            uint32_t registration_count = 0u;
            int64_t elapsed_ms = 0;
            // Populated for successful commits and ReplaceFailed diagnostics.
            // Callers that act on registrations must gate on success() first.
            std::vector<wz::asset::AssetGraphDraftRegistration>
                registrations;

            [[nodiscard]] bool success() const noexcept
            {
                return status == Status::Success;
            }
        };

        // On success, replaces the registered authoring graph and reloads draft
        // to a clean Existing baseline. On failure, the registered graph is
        // unchanged; MaterializeFailed leaves the draft unmaterialized, while
        // ReplaceFailed may leave materialized keys in the caller's draft.
        AssetGraphDraftCommitReport commit_asset_graph_draft(
            wz::asset::AssetGraphDraft& draft);

        ResolveReport resolve_all(
            std::source_location caller =
                std::source_location::current());
        // Like resolve_all, but resolves the graph's SINKS as roots through the
        // disk-cache provider with CachePreferred: a cached heavy asset is served
        // from the (possibly sealed) cache and its source prerequisites are never
        // demanded — so the runtime resolves with heavy sources stripped. Coverage
        // matches resolve_all (sinks + prerequisites = every node). Issue #334.
        ResolveReport resolve_all_cached(
            std::source_location caller =
                std::source_location::current());
        ResolveReport resolve_runtime(
            std::source_location caller =
                std::source_location::current());
        ResolveReport resolve_editor(
            std::source_location caller =
                std::source_location::current());
        ResolveReport resolve_demanded(
            wz::asset::ResolvePolicy policy,
            std::source_location caller =
                std::source_location::current());

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

        wz::asset::AssetKeyFactoryFn draft_key_factory() const;

        // Explicit context for the UI-agnostic graph-authoring verbs in
        // engine/assets/authoring/asset_graph_authoring.h: the compiler registry
        // plus this library's file-path resolver. Centralizes the wiring so call
        // sites (e.g. the scene editor) don't re-derive it. The returned context
        // borrows this library and must not outlive it.
        authoring::GraphAuthoringContext graph_authoring_context();

        wz::Logger& logger() const noexcept
        {
            return logger_;
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

        MeshDerivedFieldAssetModule&       mesh_derived_fields()       { return mesh_derived_fields_; }
        const MeshDerivedFieldAssetModule& mesh_derived_fields() const { return mesh_derived_fields_; }

        MeshSparseOperatorAssetModule&       mesh_sparse_operators()       { return mesh_sparse_operators_; }
        const MeshSparseOperatorAssetModule& mesh_sparse_operators() const { return mesh_sparse_operators_; }

        GpuSparseMeshAssetModule&       gpu_sparse_meshes()       { return gpu_sparse_meshes_; }
        const GpuSparseMeshAssetModule& gpu_sparse_meshes() const { return gpu_sparse_meshes_; }

        MeshClusterHierarchyAssetModule&       mesh_cluster_hierarchies()       { return mesh_cluster_hierarchies_; }
        const MeshClusterHierarchyAssetModule& mesh_cluster_hierarchies() const { return mesh_cluster_hierarchies_; }

        TerrainAssetModule&            terrains()       { return terrains_; }
        const TerrainAssetModule&      terrains() const { return terrains_; }

        TerrainVisualProxyAssetModule&       terrain_visual_proxies()       { return terrain_visual_proxies_; }
        const TerrainVisualProxyAssetModule& terrain_visual_proxies() const { return terrain_visual_proxies_; }

        CollisionAssetModule&       collisions()       { return collisions_; }
        const CollisionAssetModule& collisions() const { return collisions_; }

        PlacementAssetModule&       placements()       { return placements_; }
        const PlacementAssetModule& placements() const { return placements_; }

        PlacedFieldAssetModule&       placed_fields()       { return placed_fields_; }
        const PlacedFieldAssetModule& placed_fields() const { return placed_fields_; }

        ClipmapLatticeScheduleAssetModule&       clipmap_lattice_schedules()       { return clipmap_lattice_schedules_; }
        const ClipmapLatticeScheduleAssetModule& clipmap_lattice_schedules() const { return clipmap_lattice_schedules_; }

        AtmosphereAssetModule&       atmospheres()       { return atmospheres_; }
        const AtmosphereAssetModule& atmospheres() const { return atmospheres_; }

        EnvironmentAssetModule&       environments()       { return environments_; }
        const EnvironmentAssetModule& environments() const { return environments_; }

        AudioClipAssetModule&       audio_clips()       { return audio_clips_; }
        const AudioClipAssetModule& audio_clips() const { return audio_clips_; }

        AudioClipBankAssetModule&       audio_clip_banks()       { return audio_clip_banks_; }
        const AudioClipBankAssetModule& audio_clip_banks() const { return audio_clip_banks_; }

        AudioRenderableAssetModule&       audio_renderables()       { return audio_renderables_; }
        const AudioRenderableAssetModule& audio_renderables() const { return audio_renderables_; }

        GaussianSplatAssetModule&       gaussian_splats()         { return gaussian_splats_; }
        const GaussianSplatAssetModule& gaussian_splats()   const { return gaussian_splats_; }

        PuppetAssetModule&              puppets()                 { return puppets_; }
        const PuppetAssetModule&        puppets()           const { return puppets_; }

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

        RenderBindingLayoutAssetModule&       render_binding_layouts()       { return render_binding_layouts_; }
        const RenderBindingLayoutAssetModule& render_binding_layouts() const { return render_binding_layouts_; }

        RenderableAssetModule&       renderables()        { return renderables_; }
        const RenderableAssetModule& renderables()  const { return renderables_; }

        RenderProgramAssetModule&       render_programs()       { return render_programs_; }
        const RenderProgramAssetModule& render_programs() const { return render_programs_; }

        ComputePipelineAssetModule&       compute_pipelines()       { return compute_pipelines_; }
        const ComputePipelineAssetModule& compute_pipelines() const { return compute_pipelines_; }

        LightAssetModule&       lights()       { return lights_; }
        const LightAssetModule& lights() const { return lights_; }

        SkyGaussianAssetModule&       sky_gaussians()       { return sky_gaussians_; }
        const SkyGaussianAssetModule& sky_gaussians() const { return sky_gaussians_; }

        StarCatalogAssetModule&       star_catalogs()       { return star_catalogs_; }
        const StarCatalogAssetModule& star_catalogs() const { return star_catalogs_; }

        TextureAssetModule&       textures()       { return textures_; }
        const TextureAssetModule& textures() const { return textures_; }

        SceneAssetModule&       scenes()       { return scenes_; }
        const SceneAssetModule& scenes() const { return scenes_; }

        // ── Direct access ─────────────────────────────────────────────────────────

        wz::asset::AssetSystem&       system()       { return system_; }
        const wz::asset::AssetSystem& system() const { return system_; }

        GpuResidentFieldTable&       gpu_resident_fields()       { return gpu_resident_field_table_; }
        const GpuResidentFieldTable& gpu_resident_fields() const { return gpu_resident_field_table_; }

        GpuResidentMeshDataTable&       gpu_resident_mesh_data()       { return gpu_resident_mesh_data_table_; }
        const GpuResidentMeshDataTable& gpu_resident_mesh_data() const { return gpu_resident_mesh_data_table_; }

        GpuResidentSparseOperatorTable&       gpu_resident_sparse_operators()       { return gpu_resident_sparse_operator_table_; }
        const GpuResidentSparseOperatorTable& gpu_resident_sparse_operators() const { return gpu_resident_sparse_operator_table_; }

        GpuResidentSparseMeshTable&       gpu_resident_sparse_meshes()       { return gpu_resident_sparse_mesh_table_; }
        const GpuResidentSparseMeshTable& gpu_resident_sparse_meshes() const { return gpu_resident_sparse_mesh_table_; }

        GpuResidentMeshClusterHierarchyTable& gpu_resident_mesh_cluster_hierarchies()
        {
            return gpu_resident_mesh_cluster_hierarchy_table_;
        }
        const GpuResidentMeshClusterHierarchyTable&
            gpu_resident_mesh_cluster_hierarchies() const
        {
            return gpu_resident_mesh_cluster_hierarchy_table_;
        }

        MeshFieldComputeBackend& mesh_field_compute() { return *mesh_field_compute_; }

        bool gpu_device_valid() const { return device_.valid(); }

        // Release shared-registry RHI residency for any tracked asset that is no
        // longer in the registered set. Asset-type agnostic: it walks the generic
        // (key → identities) tracker that compilers populate when they acquire
        // resident buffers, and release()s identities whose key dropped out of the
        // current graph. Uses release() only (deferred, timeline-safe) — the
        // single collect() lives in RhiSceneRenderer::on_graph_changed(), which
        // must run AFTER this so the same shared registry reclaims both
        // renderer-side and asset-side released buffers.
        void release_unregistered_rhi_resources();

        // Record (key → identities) the asset acquired in the shared registry, so
        // release_unregistered_rhi_resources() can reclaim them on de-registration.
        // Idempotent per key. Called by compilers; no asset-type coupling here.
        void track_rhi_resources(
            const wz::asset::AssetKey& key,
            std::vector<wz::rhi::ResourceIdentity> identities);

        // Reconcile the shared rhi render-program / shader-module registries
        // (owned by EngineGpuContext) against the live registered set: keep the
        // entries whose AssetKey is still registered, release the rest. The
        // graph-swap path calls this AFTER resolve. Survivor-preserving, so a
        // same-content rebind (resolve is a cache hit, the compiler skipped) does
        // not lose the already-registered program/shaders; bounded, because
        // content-addressed names mean changed content registers a new entry and
        // this releases the stale one. No-op for a device-only library. The
        // renderer no longer clears these in on_graph_changed; the asset side owns
        // their lifecycle. Semantic registries are graph-independent and kept.
        void reconcile_rhi_render_program_registries();

        // Evict the compiled-state (cache + compiled-node) entries of every
        // key no longer in the registered set — the compiled-state half of the
        // two teardown sweeps above. Without it, a departed key that later
        // REJOINS resolves as a zero-cost cache hit and skips the compiler,
        // so the rhi registrations / residency the sweeps tore down are never
        // re-established and the asset stays dark (delete-subtree + undo;
        // #311). Same-content rebinds evict nothing, keeping their fast path.
        void evict_unregistered_compiled_state();

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
        wz::rhi::GpuResourceRegistry* gpu_resources_ = nullptr;
        // The shared GPU + rhi context (programs/shaders/semantic registries the
        // render-program compiler registers into). Null for a device-only
        // library, where the compiler skips rhi production (renderer-bridge path).
        wz::engine::rendering::EngineGpuContext* gpu_context_ = nullptr;
        // Generic (asset-type agnostic) tracker of shared-registry residency:
        // the rhi ResourceIdentitys each asset acquired, keyed by AssetKey.
        // Populated via track_rhi_resources() by compilers; drained on
        // de-registration by release_unregistered_rhi_resources().
        std::vector<std::pair<
            wz::asset::AssetKey,
            std::vector<wz::rhi::ResourceIdentity>>> rhi_resource_tracker_;
        wz::Logger&      logger_;
        wz::fs::Path     resource_root_;

        // Per-resolve tallies behind the resolve summary line.
        //
        // A CacheHit is the IN-MEMORY memo -- cache_.lookup plus a matching
        // compiled_nodes_ entry -- not the disk cache. A cold start with no
        // disk cache still produces them, because a node reached twice through
        // different dependency paths compiles once and is memoized after: the
        // first real run logged 143 compiled and 153 already_resolved. The
        // summary says already_resolved= for exactly that reason -- "cached"
        // on a line that also prints cache_root=<empty> reads as a disk hit
        // and is not one.
        //
        // Either way it is the system reporting that it did nothing, and
        // one line per node made that 907 of 1198 lines in a real session --
        // 76% of the log announcing non-events, which is the same reason the
        // D3D12 InfoQueue had to stop repeating itself. Counted and summarised
        // instead. Real compiles still get a line each: there are ~150 of them
        // and they carry timings, which is the part worth reading.
        //
        // Reset by whichever of the two resolve brackets runs -- resolve_all or
        // resolve_roots_with_report -- so a pass cannot inherit the previous
        // one's counts.
        uint32_t resolve_compiled_count_ = 0;
        uint32_t resolve_cached_count_ = 0;
        EngineAssetCacheSettings cache_settings_;

        ScalarFieldTable            scalar_fields_table_;
        VectorFieldTable            vector_fields_table_;
        CSVTable                    csv_table_;
        JSONTable                   json_table_;
        TOMLTable                   toml_table_;
        MeshTable                   mesh_table_;
        MeshDerivedFieldTable       mesh_derived_field_table_;
        MeshSparseOperatorTable     mesh_sparse_operator_table_;
        GpuSparseMeshTable          gpu_sparse_mesh_table_;
        MeshClusterHierarchyTable   mesh_cluster_hierarchy_table_;
        GpuResidentFieldTable       gpu_resident_field_table_;
        GpuResidentMeshDataTable    gpu_resident_mesh_data_table_;
        GpuResidentSparseOperatorTable gpu_resident_sparse_operator_table_;
        GpuResidentSparseMeshTable  gpu_resident_sparse_mesh_table_;
        GpuResidentMeshClusterHierarchyTable
            gpu_resident_mesh_cluster_hierarchy_table_;
        TerrainAssetTable           terrain_table_;
        TerrainVisualProxyTable      terrain_visual_proxy_table_;
        CollisionAssetTable         collision_table_;
        PlacementTable              placement_table_;
        PlacedFieldTable            placed_field_table_;
        ClipmapLatticeScheduleTable clipmap_lattice_schedule_table_;
        AtmosphereTable             atmosphere_table_;
        EnvironmentTable            environment_table_;
        AudioClipTable              audio_clip_table_;
        AudioClipBankTable          audio_clip_bank_table_;
        AudioRenderableTable        audio_renderable_table_;
        GaussianSplatCloudTable     gaussian_splat_cloud_table_;
        GaussianSplatColorLODTable  gaussian_splat_color_lod_table_;
        PuppetTable                 puppet_table_;
        DataTable                   data_table_;
        DiagnosticResampledTimeSeriesTable  diagnostic_resampled_time_series_table_;
        DiagnosticTimeframeSummaryTable     diagnostic_timeframe_summary_table_;
        CSVExportTable                      csv_export_table_;
        MeshRenderStyleTable        mesh_render_style_table_;
        RenderBindingLayoutTable    render_binding_layout_table_;
        RenderableAssetTable        renderable_table_;
        RhiRenderableTable          rhi_renderable_table_;
        RenderProgramTable          render_program_table_;
        ComputePipelineTable        compute_pipeline_table_;
        DirectLightTable            direct_light_table_;
        AmbientLightingTable        ambient_lighting_table_;
        HDRIEnvironmentTable        hdri_environment_table_;
        SkyGaussianTable            sky_gaussian_table_;
        wz::engine::starfield::StarCatalogTable star_catalog_table_;
        TextureTable                texture_table_;
        SceneAssetTable             scene_table_;

        // Before system_: the wavelet compiler lambda captures a reference to
        // the backend, and the gpu_resident_field_table_ destructor path uses
        // it as well.
        std::unique_ptr<MeshFieldComputeBackend> mesh_field_compute_;

        wz::asset::AssetSystem system_;

        FileCarrierAssetModule      files_;
        ShaderAssetModule           shaders_;
        ScalarFieldAssetModule      scalar_fields_;
        VectorFieldAssetModule      vector_fields_;
        CSVAssetModule              csv_;
        JSONAssetModule             json_;
        TOMLAssetModule             toml_;
        MeshAssetModule             meshes_;
        MeshDerivedFieldAssetModule mesh_derived_fields_;
        MeshSparseOperatorAssetModule mesh_sparse_operators_;
        GpuSparseMeshAssetModule    gpu_sparse_meshes_;
        MeshClusterHierarchyAssetModule mesh_cluster_hierarchies_;
        TerrainAssetModule          terrains_;
        TerrainVisualProxyAssetModule terrain_visual_proxies_;
        CollisionAssetModule        collisions_;
        PlacementAssetModule        placements_;
        PlacedFieldAssetModule      placed_fields_;
        ClipmapLatticeScheduleAssetModule clipmap_lattice_schedules_;
        AtmosphereAssetModule       atmospheres_;
        EnvironmentAssetModule      environments_;
        AudioClipAssetModule        audio_clips_;
        AudioClipBankAssetModule    audio_clip_banks_;
        AudioRenderableAssetModule  audio_renderables_;
        GaussianSplatAssetModule    gaussian_splats_;
        GaussianSplatColorLODAssetModule gaussian_splat_color_lods_;
        PuppetAssetModule           puppets_;
        DataTableAssetModule        data_tables_;
        DiagnosticResampledTimeSeriesAssetModule diagnostic_resampled_time_series_;
        DiagnosticTimeframeSummaryAssetModule    diagnostic_timeframe_summaries_;
        CSVExportAssetModule                     csv_export_;
        MeshRenderStyleAssetModule  mesh_render_styles_;
        RenderBindingLayoutAssetModule render_binding_layouts_;
        RenderableAssetModule       renderables_;
        RenderProgramAssetModule    render_programs_;
        ComputePipelineAssetModule  compute_pipelines_;
        LightAssetModule            lights_;
        SkyGaussianAssetModule      sky_gaussians_;
        StarCatalogAssetModule      star_catalogs_;
        TextureAssetModule          textures_;
        SceneAssetModule            scenes_;

        ResolveReport resolve_roots_with_report(
            std::span<const wz::asset::AssetKey> roots,
            wz::asset::ResolvePolicy policy,
            const char* label,
            std::source_location caller);
        std::vector<wz::asset::AssetKey> active_demand_roots(
            std::span<const wz::asset::DemandRoot> roots) const;
        std::vector<wz::asset::AssetKey> all_active_demand_roots() const;

        EngineAssetLibrary(
            wz::gpu::Device& device,
            wz::engine::rendering::EngineGpuContext* gpu_context,
            wz::Logger& logger,
            wz::fs::Path resource_root,
            EngineAssetCacheSettings cache_settings);
    };

} // namespace wz::engine::assets
