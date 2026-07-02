#include <engine/assets/light/light_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/hdri/hdri_image_loader.h>
#include <engine/assets/hdri/hdri_lighting_metadata.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr std::array<std::string_view, 3> kDirectLightKindOptions = {
            "Directional",
            "Point",
            "Spot",
        };

        constexpr std::array<std::string_view, 2>
            kAmbientLightingModeOptions = {
                "Constant",
                "Field modulated",
            };

        constexpr std::array<std::string_view, 2>
            kAmbientLightingDomainMappingOptions = {
                "Terrain UV",
                "World XZ",
            };

        constexpr std::array<std::string_view, 3> kHDRIFormatOptions = {
            "Auto",
            "Radiance HDR",
            "OpenEXR",
        };

        std::array<float, 3> param_float3(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            const float fallback[3])
        {
            return params.get<std::array<float, 3>>(
                name,
                { fallback[0], fallback[1], fallback[2] });
        }

        DirectLightCompileDesc direct_light_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            DirectLightCompileDesc desc{};
            const int64_t kind =
                params.get<int64_t>("kind", static_cast<int64_t>(desc.kind));
            if (kind >= 0
                && kind < static_cast<int64_t>(kDirectLightKindOptions.size()))
            {
                desc.kind = static_cast<DirectLightKind>(kind);
            }
            const auto color = param_float3(params, "color", desc.color);
            desc.color[0] = color[0];
            desc.color[1] = color[1];
            desc.color[2] = color[2];
            desc.intensity = params.get<float>("intensity", desc.intensity);
            desc.range = params.get<float>("range", desc.range);
            desc.inner_cone_radians =
                params.get<float>(
                    "inner_cone_radians",
                    desc.inner_cone_radians);
            desc.outer_cone_radians =
                params.get<float>(
                    "outer_cone_radians",
                    desc.outer_cone_radians);
            return desc;
        }

        AmbientLightingCompileDesc ambient_lighting_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            AmbientLightingCompileDesc desc{};
            const int64_t mode =
                params.get<int64_t>("mode", static_cast<int64_t>(desc.mode));
            if (mode >= 0
                && mode
                    < static_cast<int64_t>(
                        kAmbientLightingModeOptions.size()))
            {
                desc.mode = static_cast<AmbientLightingMode>(mode);
            }
            const auto color = param_float3(params, "color", desc.color);
            desc.color[0] = color[0];
            desc.color[1] = color[1];
            desc.color[2] = color[2];
            desc.intensity = params.get<float>("intensity", desc.intensity);
            const int64_t domain_mapping =
                params.get<int64_t>(
                    "domain_mapping",
                    static_cast<int64_t>(desc.domain_mapping));
            if (domain_mapping >= 0
                && domain_mapping
                    < static_cast<int64_t>(
                        kAmbientLightingDomainMappingOptions.size()))
            {
                desc.domain_mapping =
                    static_cast<AmbientLightingDomainMapping>(
                        domain_mapping);
            }
            return desc;
        }

        HDRIEnvironmentCompileDesc hdri_environment_desc_from_params(
            const wz::asset::ParamBlock& params,
            wz::asset::AssetKey source_file)
        {
            HDRIEnvironmentCompileDesc desc{};
            desc.source_file = source_file;

            const int64_t format =
                params.get<int64_t>(
                    "format",
                    static_cast<int64_t>(desc.format));
            if (format >= 0
                && format < static_cast<int64_t>(kHDRIFormatOptions.size()))
            {
                desc.format = static_cast<HDRIEnvironmentFormat>(format);
            }

            desc.exposure = params.get<float>("exposure", desc.exposure);
            desc.rotation_x_radians =
                params.get<float>(
                    "rotation_x_radians",
                    desc.rotation_x_radians);
            desc.rotation_y_radians =
                params.get<float>(
                    "rotation_y_radians",
                    desc.rotation_y_radians);
            desc.rotation_z_radians =
                params.get<float>(
                    "rotation_z_radians",
                    desc.rotation_z_radians);
            desc.lighting_intensity =
                params.get<float>(
                    "lighting_intensity",
                    desc.lighting_intensity);
            desc.reflection_intensity =
                params.get<float>(
                    "reflection_intensity",
                    desc.reflection_intensity);
            desc.background_intensity =
                params.get<float>(
                    "background_intensity",
                    desc.background_intensity);
            desc.lighting_sample_resolution =
                params.get<uint32_t>(
                    "lighting_sample_resolution",
                    desc.lighting_sample_resolution);

            const auto environment_color =
                param_float3(
                    params,
                    "environment_light_color",
                    desc.environment_light_color);
            desc.environment_light_color[0] = environment_color[0];
            desc.environment_light_color[1] = environment_color[1];
            desc.environment_light_color[2] = environment_color[2];
            desc.environment_light_intensity =
                params.get<float>(
                    "environment_light_intensity",
                    desc.environment_light_intensity);

            const auto dominant_direction =
                param_float3(
                    params,
                    "dominant_light_direction",
                    desc.dominant_light_direction);
            desc.dominant_light_direction[0] = dominant_direction[0];
            desc.dominant_light_direction[1] = dominant_direction[1];
            desc.dominant_light_direction[2] = dominant_direction[2];

            const auto dominant_color =
                param_float3(
                    params,
                    "dominant_light_color",
                    desc.dominant_light_color);
            desc.dominant_light_color[0] = dominant_color[0];
            desc.dominant_light_color[1] = dominant_color[1];
            desc.dominant_light_color[2] = dominant_color[2];
            desc.dominant_light_intensity =
                params.get<float>(
                    "dominant_light_intensity",
                    desc.dominant_light_intensity);
            desc.dominant_light_confidence =
                params.get<float>(
                    "dominant_light_confidence",
                    desc.dominant_light_confidence);
            return desc;
        }

        // #201: publish the HDRI environment image onto the shared wozzits-rhi
        // registry as an RGBA32Float Texture2D resource (the surrogate pattern —
        // the registry owns the GPU texture, the asset is its surrogate). Mirrors
        // publish_resident_scalar_field and replaces the retired legacy
        // upload_hdr_image_texture; the 3/4-channel float image is expanded to
        // RGBA (alpha 1.0 when the source is RGB). Best-effort: a failure is
        // logged and does not fail the compile (the environment's lighting
        // metadata still resolves). A filtered/equirect consumer (a later #195
        // render path) declares its own LinearWrap static sampler on the SRG.
        void publish_resident_hdri_environment(
            const wz::asset::AssetKey& key,
            const HDRImageData& image,
            wz::rhi::GpuResourceRegistry& gpu_resources,
            const RhiResourceTracker& rhi_resource_tracker,
            wz::Logger& logger)
        {
            if (!image.valid() || image.channels < 3u) {
                return;
            }

            const auto started = std::chrono::steady_clock::now();

            const uint64_t pixel_count =
                static_cast<uint64_t>(image.width)
                * static_cast<uint64_t>(image.height);
            std::vector<float> rgba(
                static_cast<size_t>(pixel_count) * 4u, 1.0f);
            for (uint64_t p = 0; p < pixel_count; ++p) {
                const size_t src =
                    static_cast<size_t>(p) * image.channels;
                const size_t dst = static_cast<size_t>(p) * 4u;
                rgba[dst + 0] = image.pixels[src + 0];
                rgba[dst + 1] = image.pixels[src + 1];
                rgba[dst + 2] = image.pixels[src + 2];
                rgba[dst + 3] =
                    image.channels >= 4u ? image.pixels[src + 3] : 1.0f;
            }

            wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::texture_2d(
                image.width,
                image.height,
                wz::rhi::TextureFormat::RGBA32Float,
                wz::rhi::ResourceUsage_Sampled);
            desc.cpu_access = wz::rhi::ResourceCpuAccess::WriteOnce;
            desc.identity = wz::rhi::ResourceIdentity{
                rhi_asset_identity(key, "environment_map_texture"),
                {},
            };

            const wz::rhi::GpuResourceHandle handle = gpu_resources.acquire(desc);
            const uint64_t byte_count =
                static_cast<uint64_t>(rgba.size()) * sizeof(float);
            const bool uploaded =
                handle.valid()
                && gpu_resources.update(handle, rgba.data(), byte_count);
            if (!uploaded) {
                if (handle.valid()) {
                    gpu_resources.release(handle);
                }
                logger.warn("HDRI environment RHI resident upload failed");
                return;
            }

            if (rhi_resource_tracker) {
                rhi_resource_tracker(key, { desc.identity });
            }

            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            logger.info(
                "asset compile: HDRI environment RHI resident upload "
                + std::to_string(image.width) + "x"
                + std::to_string(image.height)
                + " upload_ms=" + std::to_string(elapsed_ms));
        }
    }

    void register_light_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DirectLightTable& direct_light_table,
        AmbientLightingTable& ambient_lighting_table,
        HDRIEnvironmentTable& hdri_environment_table,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kDirectLightSchema,
            .output_type = kAssetTypeDirectLight,
            .parameters = {
                {
                    .name = "kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Kind",
                    .default_num =
                        static_cast<double>(
                            DirectLightKind::Directional),
                    .options = kDirectLightKindOptions,
                },
                {
                    .name = "color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Color",
                    .default_num = 1.0,
                },
                {
                    .name = "intensity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Intensity",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 32.0,
                },
                {
                    .name = "range",
                    .type = wz::asset::ParamType::Float,
                    .label = "Range",
                    .default_num = 10.0,
                    .min = 0.0,
                    .max = 1000.0,
                },
                {
                    .name = "inner_cone_radians",
                    .type = wz::asset::ParamType::Float,
                    .label = "Inner cone radians",
                    .default_num = 0.4,
                    .min = 0.0,
                    .max = 3.14159,
                },
                {
                    .name = "outer_cone_radians",
                    .type = wz::asset::ParamType::Float,
                    .label = "Outer cone radians",
                    .default_num = 0.8,
                    .min = 0.0,
                    .max = 3.14159,
                },
            },
            .compile = [&logger, &direct_light_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                DirectLightCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<DirectLightCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc = direct_light_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }
                if (!desc) {
                    logger.error(
                        "direct light node missing DirectLightCompileDesc or "
                        "ParamBlock");
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
            .parameters = {
                {
                    .name = "mode",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Mode",
                    .default_num =
                        static_cast<double>(
                            AmbientLightingMode::Constant),
                    .options = kAmbientLightingModeOptions,
                },
                {
                    .name = "color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Color",
                    .default_num = 1.0,
                },
                {
                    .name = "intensity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Intensity",
                    .default_num = 0.2,
                    .min = 0.0,
                    .max = 8.0,
                },
                {
                    .name = "domain_mapping",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain mapping",
                    .default_num =
                        static_cast<double>(
                            AmbientLightingDomainMapping::TerrainUV),
                    .options = kAmbientLightingDomainMappingOptions,
                },
            },
            .compile = [&logger, &ambient_lighting_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                AmbientLightingCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<AmbientLightingCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            ambient_lighting_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }
                if (!desc) {
                    logger.error(
                        "ambient lighting node missing "
                        "AmbientLightingCompileDesc or ParamBlock");
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
            .input_ports = {
                { "source_image", kAssetTypeRawFile },
            },
            .parameters = {
                {
                    .name = "format",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Format",
                    .default_num =
                        static_cast<double>(HDRIEnvironmentFormat::Auto),
                    .options = kHDRIFormatOptions,
                },
                {
                    .name = "exposure",
                    .type = wz::asset::ParamType::Float,
                    .label = "Exposure",
                    .default_num = 0.0,
                    .min = -16.0,
                    .max = 16.0,
                },
                {
                    .name = "rotation_x_radians",
                    .type = wz::asset::ParamType::Float,
                    .label = "Rotation X radians",
                    .default_num = 0.0,
                },
                {
                    .name = "rotation_y_radians",
                    .type = wz::asset::ParamType::Float,
                    .label = "Rotation Y radians",
                    .default_num = 0.0,
                },
                {
                    .name = "rotation_z_radians",
                    .type = wz::asset::ParamType::Float,
                    .label = "Rotation Z radians",
                    .default_num = 0.0,
                },
                {
                    .name = "lighting_intensity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Lighting intensity",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 8.0,
                },
                {
                    .name = "reflection_intensity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Reflection intensity",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 8.0,
                },
                {
                    .name = "background_intensity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Background intensity",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 8.0,
                },
                {
                    .name = "lighting_sample_resolution",
                    .type = wz::asset::ParamType::Int,
                    .label = "Lighting sample resolution",
                    .default_num = 1024.0,
                    .min = 16.0,
                    .max = 4096.0,
                },
                {
                    .name = "environment_light_color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Environment light color",
                    .default_num = 1.0,
                },
                {
                    .name = "environment_light_intensity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Environment light intensity",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 32.0,
                },
                {
                    .name = "dominant_light_direction",
                    .type = wz::asset::ParamType::Float3,
                    .label = "Dominant light direction",
                    .default_num = 0.0,
                },
                {
                    .name = "dominant_light_color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Dominant light color",
                    .default_num = 1.0,
                },
                {
                    .name = "dominant_light_intensity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Dominant light intensity",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 32.0,
                },
                {
                    .name = "dominant_light_confidence",
                    .type = wz::asset::ParamType::Float,
                    .label = "Dominant light confidence",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 1.0,
                },
            },
            .compile = [&logger, &hdri_environment_table,
                        gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                if (dep_nodes.size() != 1) {
                    logger.error(
                        "HDRI environment node must have one source file dependency");
                    return compile_failed_node(input);
                }

                HDRIEnvironmentCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<HDRIEnvironmentCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            hdri_environment_desc_from_params(
                                *params,
                                dep_nodes[0].key);
                        desc = &param_desc;
                    }
                }
                if (!desc) {
                    logger.error(
                        "HDRI environment node missing "
                        "HDRIEnvironmentCompileDesc or ParamBlock");
                    return compile_failed_node(input);
                }
                if (!desc->valid()) {
                    logger.error("HDRI environment compile desc is invalid");
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
                const bool has_explicit_lighting_metadata =
                    desc->environment_light_intensity > 0.0f
                    || desc->dominant_light_intensity > 0.0f;
                const bool can_decode_exr =
                    !has_explicit_lighting_metadata
                    && (desc->format == HDRIEnvironmentFormat::Auto
                        || desc->format == HDRIEnvironmentFormat::OpenEXR);
                if (can_decode_exr) {
                    static std::mutex cache_mutex;
                    static std::unordered_map<uint64_t, HDRILightingMetadata>
                        metadata_cache;

                    const uint64_t cache_key =
                        desc->source_file.content_hash.lo
                        ^ desc->source_file.deps_hash.lo
                        ^ desc->source_file.schema_hash.lo
                        ^ static_cast<uint64_t>(
                            desc->lighting_sample_resolution);

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
                                desc->lighting_sample_resolution,
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

                // #201: publish GPU residency for the environment image. Only
                // OpenEXR is decodable at this boundary today; a RadianceHDR
                // decoder would slot in here the same way. Residency is
                // best-effort and independent of the lighting-metadata path
                // above; a decode failure for a non-EXR source is not fatal
                // (the environment still resolves for its lighting scalars).
                if (gpu_resources != nullptr
                    && (desc->format == HDRIEnvironmentFormat::Auto
                        || desc->format == HDRIEnvironmentFormat::OpenEXR))
                {
                    HDRImageData image{};
                    std::string error;
                    if (load_openexr_image_from_memory(*bytes, image, error)) {
                        publish_resident_hdri_environment(
                            input.key,
                            image,
                            *gpu_resources,
                            rhi_resource_tracker,
                            logger);
                    }
                    else if (desc->format == HDRIEnvironmentFormat::OpenEXR) {
                        logger.warn(
                            "HDRI environment residency skipped: OpenEXR "
                            "decode failed: " + error);
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
