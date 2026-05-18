// src/engine/assets/gaussian_splat/gaussian_splat_compilers.cpp

#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_ply_importer.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <span>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        GaussianSplatCloudData make_debug_sphere_cloud(
            const ProceduralGaussianSplatCloudCompileDesc& desc)
        {
            GaussianSplatCloudData cloud{};
            cloud.splats.reserve(desc.count);

            // 3DGS encoding constants.
            // opacity stored as logit; logit(0.9) ≈ 2.197 gives a clearly visible splat.
            constexpr float kLogitOpacity = 2.1972245773f;
            // scale stored as log.
            const float log_scale = std::log(desc.splat_scale);
            // color stored as SH DC coefficients: f_dc = (display - 0.5) / SH_C0
            constexpr float SH_C0 = 0.28209479177387814f;

            // Deterministic Fibonacci-ish sphere distribution. This is not random;
            // identical desc produces identical splat order and values.
            constexpr float pi = 3.14159265358979323846f;
            const float golden_angle = pi * (3.0f - std::sqrt(5.0f));

            for (uint32_t i = 0; i < desc.count; ++i) {
                const float t =
                    (desc.count > 1)
                    ? static_cast<float>(i) / static_cast<float>(desc.count - 1)
                    : 0.0f;

                const float y = 1.0f - 2.0f * t;
                const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
                const float theta = golden_angle * static_cast<float>(i);

                const float x = std::cos(theta) * r;
                const float z = std::sin(theta) * r;

                GaussianSplat splat{};
                splat.position[0] = x * desc.radius;
                splat.position[1] = y * desc.radius;
                splat.position[2] = z * desc.radius;

                splat.scale[0] = log_scale;
                splat.scale[1] = log_scale;
                splat.scale[2] = log_scale;

                // Identity quaternion: rot_0=w=1, rot_1=x=0, rot_2=y=0, rot_3=z=0.
                splat.rotation[0] = 1.0f;

                splat.opacity = kLogitOpacity;

                // Simple diagnostic color from normalized position, encoded as SH DC.
                const float display_r = 0.5f + 0.5f * x;
                const float display_g = 0.5f + 0.5f * y;
                const float display_b = 0.5f + 0.5f * z;
                splat.color_dc[0] = (display_r - 0.5f) / SH_C0;
                splat.color_dc[1] = (display_g - 0.5f) / SH_C0;
                splat.color_dc[2] = (display_b - 0.5f) / SH_C0;

                cloud.splats.push_back(splat);
            }

            const float b = desc.radius + desc.splat_scale;
            cloud.bounds.min[0] = -b;
            cloud.bounds.min[1] = -b;
            cloud.bounds.min[2] = -b;
            cloud.bounds.max[0] = b;
            cloud.bounds.max[1] = b;
            cloud.bounds.max[2] = b;
            cloud.bounds.valid = true;

            cloud.opacity_min = kLogitOpacity;
            cloud.opacity_max = kLogitOpacity;
            cloud.scale_min = log_scale;
            cloud.scale_max = log_scale;
            cloud.f_rest_count = 0;

            return cloud;
        }

        wz::asset::AssetNode compile_ply_gaussian_splat_cloud_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            wz::Logger& logger,
            GaussianSplatCloudTable& table)
        {
            if (dep_nodes.size() != 1) {
                logger.error("PLY gaussian splat cloud should have exactly one file dependency");
                return compile_failed_node(input);
            }

            const auto* bytes =
                std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

            if (!bytes || bytes->empty()) {
                logger.error("PLY gaussian splat file dependency has no bytes");
                return compile_failed_node(input);
            }

            const GaussianSplatImportResult import_result =
                import_gaussian_splat_ply_bytes({ bytes->data(), bytes->size() });

            if (!import_result.ok) {
                logger.error("failed to import PLY gaussian splat cloud: " + import_result.error);
                return compile_failed_node(input);
            }

            GaussianSplatCloudData data = import_result.cloud;

            if (!data.valid()) {
                logger.error("PLY gaussian splat importer produced invalid cloud");
                return compile_failed_node(input);
            }

            wz::asset::ResourceHandle handle = table.add(std::move(data));
            if (!handle.valid()) {
                logger.error("failed to store PLY gaussian splat cloud");
                return compile_failed_node(input);
            }

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    } // anaonymous namespace

    void register_gaussian_splat_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kProceduralGaussianSplatCloudSchema,
            .output_type = kAssetTypeGaussianSplatCloud,
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<ProceduralGaussianSplatCloudCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("procedural gaussian splat cloud missing compile desc");
                    return compile_failed_node(input);
                }

                if (!dep_nodes.empty()) {
                    logger.error("procedural gaussian splat cloud should not have dependencies");
                    return compile_failed_node(input);
                }

                if (desc->count == 0) {
                    logger.error("procedural gaussian splat cloud has zero count");
                    return compile_failed_node(input);
                }

                if (desc->radius <= 0.0f) {
                    logger.error("procedural gaussian splat cloud has non-positive radius");
                    return compile_failed_node(input);
                }

                if (desc->splat_scale <= 0.0f) {
                    logger.error("procedural gaussian splat cloud has non-positive splat_scale");
                    return compile_failed_node(input);
                }

                GaussianSplatCloudData data = make_debug_sphere_cloud(*desc);

                if (!data.valid()) {
                    logger.error("procedural gaussian splat cloud produced invalid data");
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle = table.add(std::move(data));

                if (!handle.valid()) {
                    logger.error("failed to store gaussian splat cloud");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGaussianSplatFromPLYSchema,
            .output_type = kAssetTypeGaussianSplatCloud,
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_ply_gaussian_splat_cloud_node(
                    input, dep_nodes, logger, table);
            }
            });
    }
}