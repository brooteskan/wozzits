// engine/assets/engine_asset_library.cpp

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/file_carrier.h>
#include <engine/assets/key_factories/hlsl_shader.h>
#include <engine/assets/key_factories/scalar_field.h>
#include <engine/assets/key_factories/scalar_field_procedural.h>

#include <engine/assets/shader/shader_types.h>
#include <gpu/shader.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <vector>
#include <any>

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
        , scalar_fields_{}
        , system_(internal::make_engine_compiler_registry(device, logger, scalar_fields_))
        // scalar_fields_ is declared before system_ in the class, so it is
        // fully constructed by the time make_engine_compiler_registry runs.
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





    wz::asset::AssetKey EngineAssetLibrary::register_procedural_scalar_field_node(
        const ProceduralScalarFieldCompileDesc& desc,
        std::string_view name)
    {
        const wz::asset::AssetKey key = make_procedural_scalar_field_key(
            name,
            desc.width,
            desc.height,
            desc.depth,
            static_cast<uint8_t>(desc.generator),
            desc.frequency,
            desc.amplitude,
            static_cast<uint8_t>(desc.format),
            static_cast<uint8_t>(desc.domain_kind)
        );

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeScalarField;
        node.schema = kScalarFieldProceduralSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = desc;

        // No dependency vector — procedural nodes have no file prerequisite.
        if (!system_.register_asset(std::move(node))) {
            logger_.error(
                "duplicate procedural scalar field key — "
                "name and parameters must be unique: "
                + std::string(name));
            return {};
        }

        return key;
    }

    ScalarFieldAsset EngineAssetLibrary::create_procedural_scalar_field(
        const ProceduralScalarFieldDesc& desc)
    {
        ScalarFieldAsset out{};

        const ProceduralScalarFieldCompileDesc compile_desc{
            .width = desc.width,
            .height = desc.height,
            .depth = desc.depth,
            .generator = desc.generator,
            .frequency = desc.frequency,
            .amplitude = desc.amplitude,
            .format = desc.format,
            .domain_kind = desc.domain_kind,
        };

        const wz::asset::AssetKey key =
            register_procedural_scalar_field_node(compile_desc, desc.name);

        if (key == wz::asset::AssetKey{}) {
            logger_.error(
                "failed to register procedural scalar field: " + desc.name);
            return out;
        }

        out.output = key;
        return out;
    }

} // namespace wz::engine::assets
