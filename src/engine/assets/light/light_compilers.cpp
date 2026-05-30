#include <engine/assets/light/light_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/hdri/hdri_image_loader.h>
#include <engine/assets/hdri/hdri_lighting_metadata.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <mutex>
#include <string>
#include <unordered_map>

namespace wz::engine::assets::internal
{
    void register_light_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DirectLightTable& direct_light_table,
        AmbientLightingTable& ambient_lighting_table,
        HDRIEnvironmentTable& hdri_environment_table)
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

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kHDRIEnvironmentSchema,
            .output_type = kAssetTypeEnvironmentMap,
            .compile = [&logger, &hdri_environment_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<HDRIEnvironmentCompileDesc>(&input.meta);
                if (!desc) {
                    logger.error(
                        "HDRI environment node missing HDRIEnvironmentCompileDesc");
                    return compile_failed_node(input);
                }
                if (!desc->valid()) {
                    logger.error("HDRI environment compile desc is invalid");
                    return compile_failed_node(input);
                }
                if (dep_nodes.size() != 1) {
                    logger.error(
                        "HDRI environment node must have one source file dependency");
                    return compile_failed_node(input);
                }
                if (!(dep_nodes[0].key == desc->source_file)) {
                    logger.error(
                        "HDRI environment source file dependency key mismatch");
                    return compile_failed_node(input);
                }
                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);
                if (!bytes || bytes->empty()) {
                    logger.error(
                        "HDRI environment source file has no byte payload");
                    return compile_failed_node(input);
                }

                HDRIEnvironmentData environment = *desc;
                const bool can_decode_exr =
                    desc->format == HDRIEnvironmentFormat::Auto
                    || desc->format == HDRIEnvironmentFormat::OpenEXR;
                if (can_decode_exr) {
                    static std::mutex cache_mutex;
                    static std::unordered_map<uint64_t, HDRILightingMetadata>
                        metadata_cache;

                    const uint64_t cache_key =
                        desc->source_file.content_hash.lo
                        ^ desc->source_file.deps_hash.lo
                        ^ desc->source_file.schema_hash.lo;

                    HDRILightingMetadata metadata{};
                    bool found_cached = false;
                    {
                        std::lock_guard<std::mutex> lock(cache_mutex);
                        const auto found = metadata_cache.find(cache_key);
                        if (found != metadata_cache.end()) {
                            metadata = found->second;
                            found_cached = true;
                        }
                    }

                    if (!found_cached) {
                        HDRImageData image{};
                        std::string error;
                        if (!load_openexr_image_from_memory(
                                *bytes,
                                image,
                                error))
                        {
                            if (desc->format == HDRIEnvironmentFormat::OpenEXR) {
                                logger.error(
                                    "HDRI environment OpenEXR decode failed: "
                                    + error);
                                return compile_failed_node(input);
                            }
                        }
                        else {
                            if (derive_hdri_lighting_metadata(
                                image,
                                0.0f,
                                0.0f,
                                0.0f,
                                0.0f,
                                metadata))
                            {
                                std::lock_guard<std::mutex> lock(cache_mutex);
                                metadata_cache[cache_key] = metadata;
                            }
                        }
                    }

                    if (metadata.environment_light_intensity > 0.0f
                        || metadata.dominant_light_intensity > 0.0f)
                    {
                        metadata = transform_hdri_lighting_metadata(
                            metadata,
                            desc->exposure,
                            desc->rotation_x_radians,
                            desc->rotation_y_radians,
                            desc->rotation_z_radians);

                        environment.environment_light_color[0] =
                            metadata.environment_light_color[0];
                        environment.environment_light_color[1] =
                            metadata.environment_light_color[1];
                        environment.environment_light_color[2] =
                            metadata.environment_light_color[2];
                        environment.environment_light_intensity =
                            metadata.environment_light_intensity;

                        environment.dominant_light_direction[0] =
                            metadata.dominant_light_direction[0];
                        environment.dominant_light_direction[1] =
                            metadata.dominant_light_direction[1];
                        environment.dominant_light_direction[2] =
                            metadata.dominant_light_direction[2];
                        environment.dominant_light_color[0] =
                            metadata.dominant_light_color[0];
                        environment.dominant_light_color[1] =
                            metadata.dominant_light_color[1];
                        environment.dominant_light_color[2] =
                            metadata.dominant_light_color[2];
                        environment.dominant_light_intensity =
                            metadata.dominant_light_intensity;
                        environment.dominant_light_confidence =
                            metadata.dominant_light_confidence;
                    }
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = hdri_environment_table.add(environment);
                return out;
            },
        });
    }

} // namespace wz::engine::assets::internal
