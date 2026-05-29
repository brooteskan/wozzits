#include <engine/assets/light/light_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets::internal
{
    void register_light_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DirectLightTable& direct_light_table,
        AmbientLightingTable& ambient_lighting_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kDirectLightSchema,
            .output_type = kAssetTypeDirectLight,
            .compile = [&logger, &direct_light_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<DirectLightCompileDesc>(&input.meta);
                if (!desc) {
                    logger.error("direct light node missing DirectLightCompileDesc");
                    return compile_failed_node(input);
                }
                if (!desc->valid()) {
                    logger.error("direct light compile desc is invalid");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = direct_light_table.add(*desc);
                return out;
            },
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kAmbientLightingSchema,
            .output_type = kAssetTypeAmbientLighting,
            .compile = [&logger, &ambient_lighting_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<AmbientLightingCompileDesc>(&input.meta);
                if (!desc) {
                    logger.error(
                        "ambient lighting node missing AmbientLightingCompileDesc");
                    return compile_failed_node(input);
                }
                if (!desc->valid()) {
                    logger.error("ambient lighting compile desc is invalid");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = ambient_lighting_table.add(*desc);
                return out;
            },
        });
    }

} // namespace wz::engine::assets::internal
