// engine/assets/engine_asset_library.cpp

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/engine_asset_library_internal.h>

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

    } //  namespace internal


    // ─── EngineAssetLibrary ───────────────────────────────────────────────────────

    EngineAssetLibrary::EngineAssetLibrary(
        wz::gpu::Device& device,
        wz::Logger& logger,
        wz::fs::Path     resource_root)
        : device_(device)
        , logger_(logger)
        , resource_root_(std::move(resource_root))
        , scalar_fields_table_{}
        , csv_table_{}
        , gaussian_splat_cloud_table_{}
        , diagnostic_table_{}
        , diagnostic_resampled_time_series_table_{}
        , system_(internal::make_engine_compiler_registry(
            internal::EngineAssetContext{
                .device                    = device,
                .logger                    = logger,
                .scalar_fields_table       = scalar_fields_table_,
                .csv_table                 = csv_table_,
                .json_table                = json_table_,
                .toml_table                = toml_table_,
                .mesh_table                = mesh_table_,
                .gaussian_splat_cloud_table = gaussian_splat_cloud_table_,
                .diagnostic_table = diagnostic_table_,
                .diagnostic_resampled_time_series_table = diagnostic_resampled_time_series_table_,
            }))
        , files_(system_, logger_, resource_root_)
        , shaders_(system_, logger_, files_)
        , scalar_fields_(system_, logger_, files_, scalar_fields_table_)
        , csv_(system_, logger_, files_, csv_table_)
        , json_(system_, logger_, files_, json_table_)
        , toml_(system_, logger_, files_, toml_table_)
        , meshes_(system_, mesh_table_)
        , gaussian_splats_(system_, logger_, gaussian_splat_cloud_table_)
        , diagnostic_tables_(system_, logger_, diagnostic_table_)
        , diagnostic_resampled_time_series_(system_, logger_, diagnostic_resampled_time_series_table_)
    {
    }









    // ─── Public API ───────────────────────────────────────────────────────────────



    

    bool EngineAssetLibrary::commit()
    {
        if (!system_.commit()) {
            logger_.error("asset graph rejected — cycle or missing dependency");
            return false;
        }

        return true;
    }

    ResolveReport EngineAssetLibrary::resolve_all()
    {
        ResolveReport report{};

        std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>> raw_errors;
        report.resolved_count = system_.resolve_all(&raw_errors);

        for (auto& [key, err] : raw_errors) {
            logger_.error("asset resolve failed");
            report.failures.push_back({ key, err });
        }

        return report;
    }





} // namespace wz::engine::assets
