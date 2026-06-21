// engine/assets/engine_asset_library.cpp

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/engine_asset_key_factory.h>
#include <engine/assets/engine_disk_cache_provider.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/engine_gpu_context.h>

#include <wozzits/rhi/gpu_resource_registry.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace wz::engine::assets
{
    namespace internal
    {

        // ── FileSourceDesc ────────────────────────────────────────────────────────
        //
        // Stored in AssetNode::meta for file carrier nodes.
        // The file bytes are read lazily at compile time, not at registration time.

        //struct FileSourceDesc
        //{
        //    wz::fs::Path full_path;
        //    std::string  canonical_path;
        //};


        // ── compile_failed_node ───────────────────────────────────────────────────
        //
        // Returns the node unchanged (still Source stage) so resolve() sees
        // stage != Compiled and reports ResolveError::CompileFailed.

        wz::asset::AssetNode compile_failed_node(
            const wz::asset::AssetNode& input)
        {
            return input;
        }

        const char* resolve_error_name(wz::asset::ResolveError error) noexcept
        {
            switch (error) {
            case wz::asset::ResolveError::NodeNotFound:
                return "NodeNotFound";
            case wz::asset::ResolveError::CompilerNotFound:
                return "CompilerNotFound";
            case wz::asset::ResolveError::CompileFailed:
                return "CompileFailed";
            case wz::asset::ResolveError::DependencyFailed:
                return "DependencyFailed";
            case wz::asset::ResolveError::ExternalCacheMiss:
                return "ExternalCacheMiss";
            case wz::asset::ResolveError::ExternalCacheLoadFailed:
                return "ExternalCacheLoadFailed";
            }
            return "Unknown";
        }

        std::string short_asset_key_hex(const wz::asset::AssetKey& key)
        {
            const uint64_t words[]{
                key.content_hash.lo,
                key.content_hash.hi,
                key.schema_hash.lo,
                key.schema_hash.hi,
                key.compiler_hash.lo,
                key.compiler_hash.hi,
                key.deps_hash.lo,
                key.deps_hash.hi,
            };

            uint64_t state = 0x6a09e667f3bcc909ull;
            for (const uint64_t word : words) {
                state ^= word + 0x9e3779b97f4a7c15ull
                    + (state << 6)
                    + (state >> 2);
            }

            std::ostringstream out;
            out << std::hex << std::setfill('0') << std::setw(16) << state;
            return out.str();
        }

        const char* resolve_policy_name(
            wz::asset::ResolvePolicy policy) noexcept
        {
            switch (policy) {
            case wz::asset::ResolvePolicy::CacheRequired:
                return "cache-required";
            case wz::asset::ResolvePolicy::CachePreferred:
                return "cache-preferred";
            case wz::asset::ResolvePolicy::ForceRecompile:
                return "force-recompile";
            }
            return "unknown";
        }

        uint32_t graph_node_count(const wz::asset::AssetSystem& system)
        {
            const wz::asset::AssetGraph* graph = system.graph();
            return graph ? wz::core::graph::node_count(*graph) : 0u;
        }

        std::string caller_string(std::source_location caller)
        {
            std::string file = caller.file_name();
            const size_t slash = file.find_last_of("/\\");
            if (slash != std::string::npos) {
                file = file.substr(slash + 1u);
            }

            return file
                + ":"
                + std::to_string(caller.line())
                + " "
                + caller.function_name();
        }

        std::string root_keys_string(
            std::span<const wz::asset::AssetKey> roots)
        {
            if (roots.empty()) {
                return "[]";
            }

            std::string out = "[";
            for (size_t i = 0; i < roots.size(); ++i) {
                if (i != 0u) {
                    out += ", ";
                }
                out += short_asset_key_hex(roots[i]);
            }
            out += "]";
            return out;
        }

    } //  namespace internal


    // ─── EngineAssetLibrary ───────────────────────────────────────────────────────

    EngineAssetLibrary::EngineAssetLibrary(
        wz::engine::rendering::EngineGpuContext& gpu,
        wz::Logger& logger,
        wz::fs::Path resource_root)
        : EngineAssetLibrary(
            gpu,
            logger,
            std::move(resource_root),
            EngineAssetCacheSettings{})
    {
    }

    EngineAssetLibrary::EngineAssetLibrary(
        wz::engine::rendering::EngineGpuContext& gpu,
        wz::Logger& logger,
        wz::fs::Path resource_root,
        EngineAssetCacheSettings cache_settings)
        : EngineAssetLibrary(
            gpu.device,
            &gpu.resources,
            logger,
            std::move(resource_root),
            std::move(cache_settings))
    {
    }

    EngineAssetLibrary::EngineAssetLibrary(
        wz::gpu::Device& device,
        wz::Logger& logger,
        wz::fs::Path     resource_root)
        : EngineAssetLibrary(
            device,
            nullptr,
            logger,
            std::move(resource_root),
            EngineAssetCacheSettings{})
    {
    }

    EngineAssetLibrary::EngineAssetLibrary(
        wz::gpu::Device& device,
        wz::Logger& logger,
        wz::fs::Path     resource_root,
        EngineAssetCacheSettings cache_settings)
        : EngineAssetLibrary(
            device,
            nullptr,
            logger,
            std::move(resource_root),
            std::move(cache_settings))
    {
    }

    EngineAssetLibrary::EngineAssetLibrary(
        wz::gpu::Device& device,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        wz::Logger& logger,
        wz::fs::Path     resource_root,
        EngineAssetCacheSettings cache_settings)
        : device_(device)
        , gpu_resources_(gpu_resources)
        , logger_(logger)
        , resource_root_(std::move(resource_root))
        , cache_settings_(std::move(cache_settings))
        , scalar_fields_table_{}
        , vector_fields_table_{}
        , csv_table_{}
        , json_table_{}
        , toml_table_{}
        , mesh_table_{}
        , mesh_derived_field_table_{}
        , mesh_sparse_operator_table_{}
        , gpu_sparse_mesh_table_{}
        , mesh_cluster_hierarchy_table_{}
        , gpu_resident_field_table_{}
        , gpu_resident_mesh_data_table_{}
        , gpu_resident_sparse_operator_table_{}
        , gpu_resident_sparse_mesh_table_{}
        , gpu_resident_mesh_cluster_hierarchy_table_{}
        , terrain_table_{}
        , terrain_visual_proxy_table_{}
        , collision_table_{}
        , gaussian_splat_cloud_table_{}
        , gaussian_splat_color_lod_table_{}
        , data_table_{}
        , diagnostic_resampled_time_series_table_{}
        , diagnostic_timeframe_summary_table_{}
        , csv_export_table_{}
        , mesh_render_style_table_{}
        , renderable_table_{}
        , rhi_renderable_table_{}
        , render_program_table_{}
        , compute_pipeline_table_{}
        , direct_light_table_{}
        , ambient_lighting_table_{}
        , hdri_environment_table_{}
        , scene_table_{}
        , mesh_field_compute_(make_gpu_mesh_field_compute_backend(device))
        , system_(internal::make_engine_compiler_registry(
            internal::EngineAssetContext{
                .device                    = device,
                .logger                    = logger,
                .mesh_field_compute        = *mesh_field_compute_,
                .scalar_fields_table       = scalar_fields_table_,
                .vector_fields_table       = vector_fields_table_,
                .csv_table                 = csv_table_,
                .json_table                = json_table_,
                .toml_table                = toml_table_,
                .mesh_table                = mesh_table_,
                .mesh_derived_field_table  = mesh_derived_field_table_,
                .mesh_sparse_operator_table = mesh_sparse_operator_table_,
                .gpu_sparse_mesh_table = gpu_sparse_mesh_table_,
                .mesh_cluster_hierarchy_table = mesh_cluster_hierarchy_table_,
                .gpu_resident_field_table  = gpu_resident_field_table_,
                .gpu_resident_mesh_data_table = gpu_resident_mesh_data_table_,
                .gpu_resident_sparse_operator_table =
                    gpu_resident_sparse_operator_table_,
                .gpu_resident_sparse_mesh_table =
                    gpu_resident_sparse_mesh_table_,
                .gpu_resident_mesh_cluster_hierarchy_table =
                    gpu_resident_mesh_cluster_hierarchy_table_,
                .terrain_table             = terrain_table_,
                .terrain_visual_proxy_table = terrain_visual_proxy_table_,
                .collision_table           = collision_table_,
                .gaussian_splat_cloud_table = gaussian_splat_cloud_table_,
                .gaussian_splat_color_lod_table = gaussian_splat_color_lod_table_,
                .data_table = data_table_,
                .diagnostic_resampled_time_series_table = diagnostic_resampled_time_series_table_,
                .diagnostic_timeframe_summary_table     = diagnostic_timeframe_summary_table_,
                .csv_export_table    = csv_export_table_,
                .mesh_render_style_table = mesh_render_style_table_,
                .renderable_table    = renderable_table_,
                .rhi_renderable_table = rhi_renderable_table_,
                .render_program_table = render_program_table_,
                .compute_pipeline_table = compute_pipeline_table_,
                .direct_light_table = direct_light_table_,
                .ambient_lighting_table = ambient_lighting_table_,
                .hdri_environment_table = hdri_environment_table_,
                .scene_table         = scene_table_,
                .cache_settings      = cache_settings_,
                .gpu_resources       = gpu_resources_,
                .rhi_resource_tracker =
                    [this](
                        const wz::asset::AssetKey& key,
                        std::vector<wz::rhi::ResourceIdentity> identities)
                    {
                        track_rhi_resources(key, std::move(identities));
                    },
            }))
        , files_(system_, logger_, resource_root_)
        , shaders_(system_, logger_, files_)
        , scalar_fields_(system_, logger_, files_, scalar_fields_table_)
        , vector_fields_(system_, logger_, files_, vector_fields_table_)
        , csv_(system_, logger_, files_, csv_table_)
        , json_(system_, logger_, files_, json_table_)
        , toml_(system_, logger_, files_, toml_table_)
        , meshes_(system_, mesh_table_)
        , mesh_derived_fields_(system_, mesh_derived_field_table_)
        , mesh_sparse_operators_(system_, mesh_sparse_operator_table_)
        , gpu_sparse_meshes_(system_, gpu_sparse_mesh_table_)
        , mesh_cluster_hierarchies_(
            system_,
            mesh_cluster_hierarchy_table_)
        , terrains_(system_, logger_, terrain_table_, cache_settings_)
        , terrain_visual_proxies_(system_, logger_, terrain_visual_proxy_table_)
        , collisions_(system_, logger_, collision_table_)
        , gaussian_splats_(system_, logger_, gaussian_splat_cloud_table_)
        , gaussian_splat_color_lods_(system_, logger_, gaussian_splat_color_lod_table_)
        , data_tables_(system_, logger_, data_table_)
        , diagnostic_resampled_time_series_(system_, logger_, diagnostic_resampled_time_series_table_)
        , diagnostic_timeframe_summaries_(system_, logger_, diagnostic_timeframe_summary_table_)
        , csv_export_(system_, logger_, csv_export_table_)
        , mesh_render_styles_(system_, logger_, mesh_render_style_table_)
        , renderables_(
            system_,
            logger_,
            renderable_table_,
            rhi_renderable_table_)
        , render_programs_(system_, render_program_table_)
        , compute_pipelines_(system_, compute_pipeline_table_)
        , lights_(
            system_,
            logger_,
            files_,
            direct_light_table_,
            ambient_lighting_table_,
            hdri_environment_table_)
        , scenes_(
            system_,
            logger_,
            files_,
            json_,
            meshes_,
            mesh_render_styles_,
            renderables_,
            scene_table_)
    {
        if (!cache_settings_.enabled) {
            logger_.info("asset cache disabled");
        }
        else if (cache_settings_.root.empty()) {
            logger_.info("asset cache root unset");
        }
        else {
            const wz::fs::FileError err =
                wz::fs::create_directories(cache_settings_.root);
            if (err != wz::fs::FileError::None) {
                logger_.warn(
                    "asset cache directory unavailable: "
                    + cache_settings_.root);
            }
            else {
                logger_.info(
                    "asset cache ready: "
                    + cache_settings_.root);
            }
        }

        // Verbose compiler logging: one line per node visited by resolve()
        // (every compiler run, cache hit, or failure), with the asset type name.
        system_.set_resolve_logger(
            [this](const wz::asset::ResolveLogEvent& event)
            {
                const std::string name = std::string(
                    asset_type_display_name_view(event.type));
                const std::string key = internal::short_asset_key_hex(event.key);
                switch (event.phase) {
                case wz::asset::ResolveLogEvent::Phase::Compiled:
                    logger_.info(
                        "compile " + name + " key=" + key + " ("
                        + std::to_string(event.duration_us / 1000u) + "ms)");
                    break;
                case wz::asset::ResolveLogEvent::Phase::CacheHit:
                    logger_.info(
                        "compile " + name + " key=" + key + " (cached)");
                    break;
                case wz::asset::ResolveLogEvent::Phase::Failed:
                    logger_.error(
                        "compile " + name + " key=" + key + " FAILED: "
                        + internal::resolve_error_name(event.error));
                    break;
                }
            });
    }

    EngineAssetLibrary::~EngineAssetLibrary()
    {
        gpu_resident_field_table_.destroy(*mesh_field_compute_);
        gpu_resident_mesh_data_table_.destroy(*mesh_field_compute_);
        gpu_resident_sparse_operator_table_.destroy(*mesh_field_compute_);
        gpu_resident_sparse_mesh_table_.destroy(*mesh_field_compute_);
        gpu_resident_mesh_cluster_hierarchy_table_.destroy(
            *mesh_field_compute_);
    }

    void EngineAssetLibrary::track_rhi_resources(
        const wz::asset::AssetKey& key,
        std::vector<wz::rhi::ResourceIdentity> identities)
    {
        if (identities.empty()) {
            return;
        }
        for (auto& [tracked_key, tracked_identities] : rhi_resource_tracker_) {
            if (tracked_key == key) {
                tracked_identities = std::move(identities);
                return;
            }
        }
        rhi_resource_tracker_.emplace_back(key, std::move(identities));
    }

    void EngineAssetLibrary::release_unregistered_rhi_resources()
    {
        if (!gpu_resources_) {
            rhi_resource_tracker_.clear();
            return;
        }

        auto is_registered = [this](const wz::asset::AssetKey& key)
        {
            for (const wz::asset::AssetSystem::RegistrationEntry& entry :
                 system_.registered_assets())
            {
                if (entry.node.key == key) {
                    return true;
                }
            }
            return false;
        };

        // release() only — deferred and timeline-safe. The single
        // collect(UINT64_MAX) lives in RhiSceneRenderer::on_graph_changed(),
        // which the bind path runs after this; it reclaims both renderer-side
        // and asset-side released buffers from this same shared registry.
        size_t write = 0;
        for (size_t read = 0; read < rhi_resource_tracker_.size(); ++read) {
            auto& tracked = rhi_resource_tracker_[read];
            if (is_registered(tracked.first)) {
                // Survivor: keep tracking it. It is re-acquired by resolve via
                // identity dedup, so its buffers must not be released here.
                if (write != read) {
                    rhi_resource_tracker_[write] = std::move(tracked);
                }
                ++write;
                continue;
            }

            for (const wz::rhi::ResourceIdentity& identity : tracked.second) {
                const wz::rhi::GpuResourceHandle handle =
                    gpu_resources_->find(identity);
                if (handle.valid()) {
                    gpu_resources_->release(handle);
                }
            }
        }
        rhi_resource_tracker_.resize(write);
    }

    wz::asset::AssetKeyFactoryFn EngineAssetLibrary::draft_key_factory() const
    {
        return make_engine_asset_key_factory(system_.registry());
    }

    authoring::GraphAuthoringContext
        EngineAssetLibrary::graph_authoring_context()
    {
        return authoring::GraphAuthoringContext{
            system_.registry(),
            [this](const wz::fs::Path& path)
            {
                return files_.resolve_path(path);
            },
        };
    }









    // ─── Public API ───────────────────────────────────────────────────────────────



    

    bool EngineAssetLibrary::commit()
    {
        const auto started = std::chrono::steady_clock::now();
        if (!system_.commit()) {
            logger_.error("asset graph rejected — cycle or missing dependency");
            return false;
        }

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
        logger_.info(
            "asset graph commit complete ms="
            + std::to_string(elapsed));
        return true;
    }

    EngineAssetLibrary::AssetGraphDraftCommitReport
        EngineAssetLibrary::commit_asset_graph_draft(
            wz::asset::AssetGraphDraft& draft)
    {
        AssetGraphDraftCommitReport report{};

        const auto started = std::chrono::steady_clock::now();
        const wz::asset::AssetKeyFactoryFn key_factory = draft_key_factory();
        if (!wz::asset::materialize_asset_graph_draft_keys(
                draft,
                system_.registry(),
                key_factory))
        {
            report.status =
                AssetGraphDraftCommitReport::Status::MaterializeFailed;
            report.elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            logger_.error(
                "asset graph draft commit rejected during materialization");
            return report;
        }

        report.registrations =
            wz::asset::asset_graph_draft_to_registrations(
                draft,
                &system_.registry(),
                key_factory);
        report.registration_count =
            static_cast<uint32_t>(report.registrations.size());

        std::vector<wz::asset::AssetSystem::RegistrationEntry>
            replacement_entries;
        replacement_entries.reserve(report.registrations.size());
        for (const wz::asset::AssetGraphDraftRegistration& registration :
             report.registrations)
        {
            replacement_entries.push_back(
                wz::asset::AssetSystem::RegistrationEntry{
                    .node = registration.node,
                    .dep_keys = registration.dep_keys,
                });
        }

        if (!system_.replace_registered_assets(std::move(replacement_entries))) {
            report.status =
                AssetGraphDraftCommitReport::Status::ReplaceFailed;
            report.elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            logger_.error("asset graph draft commit rejected during replace");
            return report;
        }

        wz::asset::compact_asset_graph_draft(draft);
        for (wz::asset::AssetGraphDraftNode& node : draft.nodes) {
            node.state = wz::asset::AssetGraphDraftNodeState::Existing;
            node.graph_node = wz::asset::INVALID_ASSET_NODE;
        }
        draft.storage.reset();
        draft.validation_messages.clear();
        draft.dirty = false;
        wz::asset::rebuild_asset_graph_draft_indexes(draft);

        report.status = AssetGraphDraftCommitReport::Status::Success;
        report.elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
        logger_.info(
            "asset graph draft commit complete registrations="
            + std::to_string(report.registration_count)
            + " ms="
            + std::to_string(report.elapsed_ms));
        return report;
    }

    ResolveReport EngineAssetLibrary::resolve_all(
        std::source_location caller)
    {
        ResolveReport report{};

        logger_.info(
            "asset resolve_all called"
            " caller="
            + internal::caller_string(caller)
            + " committed="
            + std::to_string(system_.committed() ? 1u : 0u)
            + " graph_nodes="
            + std::to_string(internal::graph_node_count(system_))
            + " cache_entries="
            + std::to_string(system_.cache().size())
            + " cache_enabled="
            + std::to_string(cache_settings_.enabled ? 1u : 0u)
            + " cache_root="
            + (cache_settings_.root.empty() ? "<empty>" : cache_settings_.root)
            + " resource_root="
            + (resource_root_.empty() ? "<empty>" : resource_root_));

        const auto started = std::chrono::steady_clock::now();
        std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>> raw_errors;
        report.resolved_count = system_.resolve_all(&raw_errors);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
        logger_.info(
            "asset resolve_all complete resolved="
            + std::to_string(report.resolved_count)
            + " failures="
            + std::to_string(raw_errors.size())
            + " ms="
            + std::to_string(elapsed));

        for (auto& [key, err] : raw_errors) {
            logger_.error(
                "asset resolve_all failed key="
                + internal::short_asset_key_hex(key)
                + " error="
                + internal::resolve_error_name(err));
            report.failures.push_back({ key, err });
        }

        return report;
    }

    ResolveReport EngineAssetLibrary::resolve_runtime(
        std::source_location caller)
    {
        constexpr std::array<wz::asset::DemandRoot, 2> roots{
            wz::asset::DemandRoot::GPURuntime,
            wz::asset::DemandRoot::CPURuntime,
        };
        std::vector<wz::asset::AssetKey> active =
            active_demand_roots(roots);
        return resolve_roots_with_report(
            active,
            wz::asset::ResolvePolicy::CachePreferred,
            "resolve_runtime",
            caller);
    }

    ResolveReport EngineAssetLibrary::resolve_editor(
        std::source_location caller)
    {
        constexpr std::array<wz::asset::DemandRoot, 1> roots{
            wz::asset::DemandRoot::Editor,
        };
        std::vector<wz::asset::AssetKey> active =
            active_demand_roots(roots);
        return resolve_roots_with_report(
            active,
            wz::asset::ResolvePolicy::CachePreferred,
            "resolve_editor",
            caller);
    }

    ResolveReport EngineAssetLibrary::resolve_demanded(
        wz::asset::ResolvePolicy policy,
        std::source_location caller)
    {
        std::vector<wz::asset::AssetKey> active = all_active_demand_roots();
        return resolve_roots_with_report(
            active,
            policy,
            "resolve_demanded",
            caller);
    }

    ResolveReport EngineAssetLibrary::resolve_roots_with_report(
        std::span<const wz::asset::AssetKey> roots,
        wz::asset::ResolvePolicy policy,
        const char* label,
        std::source_location caller)
    {
        ResolveReport report{};

        logger_.info(
            std::string("asset ")
            + label
            + " called"
            + " caller="
            + internal::caller_string(caller)
            + " policy="
            + internal::resolve_policy_name(policy)
            + " roots="
            + std::to_string(roots.size())
            + " root_keys="
            + internal::root_keys_string(roots)
            + " committed="
            + std::to_string(system_.committed() ? 1u : 0u)
            + " graph_nodes="
            + std::to_string(internal::graph_node_count(system_))
            + " cache_entries="
            + std::to_string(system_.cache().size())
            + " cache_enabled="
            + std::to_string(cache_settings_.enabled ? 1u : 0u)
            + " cache_root="
            + (cache_settings_.root.empty() ? "<empty>" : cache_settings_.root)
            + " resource_root="
            + (resource_root_.empty() ? "<empty>" : resource_root_));

        const auto started = std::chrono::steady_clock::now();
        std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>>
            raw_errors;
        EngineDiskCacheProvider disk_cache_provider{
            cache_settings_,
            logger_,
            scalar_fields_table_,
            mesh_table_,
            mesh_derived_field_table_,
            mesh_sparse_operator_table_,
            terrain_table_,
            terrain_visual_proxy_table_,
            collision_table_,
        };
        report.resolved_count =
            system_.resolve_roots(
                roots,
                policy,
                &disk_cache_provider,
                &raw_errors);
        if (raw_errors.empty()) {
            report.evicted_count =
                system_.evict_evictable_not_demanded(roots);
        }

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
        logger_.info(
            std::string("asset ")
            + label
            + " complete roots="
            + std::to_string(roots.size())
            + " resolved="
            + std::to_string(report.resolved_count)
            + " evicted="
            + std::to_string(report.evicted_count)
            + " failures="
            + std::to_string(raw_errors.size())
            + " ms="
            + std::to_string(elapsed));

        for (auto& [key, err] : raw_errors) {
            logger_.error(
                std::string("asset ")
                + label
                + " failed key="
                + internal::short_asset_key_hex(key)
                + " error="
                + internal::resolve_error_name(err));
            report.failures.push_back({ key, err });
        }

        return report;
    }

    std::vector<wz::asset::AssetKey> EngineAssetLibrary::active_demand_roots(
        std::span<const wz::asset::DemandRoot> roots) const
    {
        std::vector<wz::asset::AssetKey> active;
        active.reserve(roots.size());
        if (!system_.committed()) {
            return active;
        }

        const auto& index = system_.index();
        for (const wz::asset::DemandRoot root : roots) {
            const wz::asset::AssetKey key = wz::asset::make_demand_root_key(root);
            if (index.find(key) != index.end()) {
                active.push_back(key);
            }
        }
        return active;
    }

    std::vector<wz::asset::AssetKey>
        EngineAssetLibrary::all_active_demand_roots() const
    {
        constexpr std::array<wz::asset::DemandRoot, 3> roots{
            wz::asset::DemandRoot::GPURuntime,
            wz::asset::DemandRoot::CPURuntime,
            wz::asset::DemandRoot::Editor,
        };
        return active_demand_roots(roots);
    }





} // namespace wz::engine::assets
