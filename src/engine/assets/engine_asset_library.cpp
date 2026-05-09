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


        // ── compute_min_max ───────────────────────────────────────────────────────
        //
        // Validates all values in the field (rejects NaN and infinity) and
        // computes min_value / max_value. Returns false and logs on the first
        // invalid value found. label is used in error messages.

        bool compute_min_max(
            const std::vector<float>& values,
            float& min_value,
            float& max_value,
            wz::Logger& logger,
            std::string_view          label)
        {
            if (values.empty()) {
                logger.error(std::string(label) + " has no values");
                return false;
            }

            min_value = std::numeric_limits<float>::max();
            max_value = std::numeric_limits<float>::lowest();

            for (uint32_t i = 0; i < static_cast<uint32_t>(values.size()); ++i) {
                const float v = values[i];

                if (std::isnan(v)) {
                    logger.error(std::string(label)
                        + " contains NaN at sample index " + std::to_string(i));
                    return false;
                }

                if (std::isinf(v)) {
                    logger.error(std::string(label)
                        + " contains infinity at sample index " + std::to_string(i));
                    return false;
                }

                if (v < min_value) min_value = v;
                if (v > max_value) max_value = v;
            }

            return true;
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
        , system_(internal::make_engine_compiler_registry(
            device,
            logger,
            scalar_fields_table_,
            csv_table_,
            json_table_,
            toml_table_))
        , files_(system_, logger_, resource_root_)
        , shaders_(system_, logger_, files_)
        , scalar_fields_(system_, logger_, files_, scalar_fields_table_)
        , csv_(system_, logger_, files_, csv_table_)
        , json_(system_, logger_, files_, json_table_)
        , toml_(system_, logger_, files_, toml_table_)
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
