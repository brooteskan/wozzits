// engine/assets/engine_asset_library.cpp

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/engine_disk_cache_provider.h>
#include <engine/assets/engine_asset_library_internal.h>

#include <array>
#include <chrono>
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

    } //  namespace internal


    // ─── EngineAssetLibrary ───────────────────────────────────────────────────────

    EngineAssetLibrary::EngineAssetLibrary(
        wz::gpu::Device& device,
        wz::Logger& logger,
        wz::fs::Path     resource_root)
        : EngineAssetLibrary(
            device,
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
        : device_(device)
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
        , render_program_table_{}
        , direct_light_table_{}
        , ambient_lighting_table_{}
        , hdri_environment_table_{}
        , scene_table_{}
        , system_(internal::make_engine_compiler_registry(
            internal::EngineAssetContext{
                .device                    = device,
                .logger                    = logger,
                .scalar_fields_table       = scalar_fields_table_,
                .vector_fields_table       = vector_fields_table_,
                .csv_table                 = csv_table_,
                .json_table                = json_table_,
                .toml_table                = toml_table_,
                .mesh_table                = mesh_table_,
                .mesh_derived_field_table  = mesh_derived_field_table_,
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
                .render_program_table = render_program_table_,
                .direct_light_table = direct_light_table_,
                .ambient_lighting_table = ambient_lighting_table_,
                .hdri_environment_table = hdri_environment_table_,
                .scene_table         = scene_table_,
                .cache_settings      = cache_settings_,
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
        , renderables_(system_, logger_, renderable_table_)
        , render_programs_(system_, render_program_table_)
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

    ResolveReport EngineAssetLibrary::resolve_all()
    {
        ResolveReport report{};

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

    ResolveReport EngineAssetLibrary::resolve_runtime()
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
            "resolve_runtime");
    }

    ResolveReport EngineAssetLibrary::resolve_editor()
    {
        constexpr std::array<wz::asset::DemandRoot, 1> roots{
            wz::asset::DemandRoot::Editor,
        };
        std::vector<wz::asset::AssetKey> active =
            active_demand_roots(roots);
        return resolve_roots_with_report(
            active,
            wz::asset::ResolvePolicy::CachePreferred,
            "resolve_editor");
    }

    ResolveReport EngineAssetLibrary::resolve_demanded(
        wz::asset::ResolvePolicy policy)
    {
        std::vector<wz::asset::AssetKey> active = all_active_demand_roots();
        return resolve_roots_with_report(active, policy, "resolve_demanded");
    }

    ResolveReport EngineAssetLibrary::resolve_roots_with_report(
        std::span<const wz::asset::AssetKey> roots,
        wz::asset::ResolvePolicy policy,
        const char* label)
    {
        ResolveReport report{};

        const auto started = std::chrono::steady_clock::now();
        std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>>
            raw_errors;
        EngineDiskCacheProvider disk_cache_provider{
            cache_settings_,
            logger_,
            scalar_fields_table_,
            mesh_table_,
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
