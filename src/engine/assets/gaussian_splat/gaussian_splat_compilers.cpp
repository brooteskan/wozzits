// src/engine/assets/gaussian_splat/gaussian_splat_compilers.cpp

#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/ply_importer.h>
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
            cloud.color_mode = GaussianSplatColorMode::RGB;
            cloud.splats.reserve(desc.count);

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

                splat.scale[0] = desc.splat_scale;
                splat.scale[1] = desc.splat_scale;
                splat.scale[2] = desc.splat_scale;

                splat.opacity = 1.0f;

                // Simple diagnostic color from normalized position.
                splat.color[0] = 0.5f + 0.5f * x;
                splat.color[1] = 0.5f + 0.5f * y;
                splat.color[2] = 0.5f + 0.5f * z;

                cloud.splats.push_back(splat);
            }

            const float b = desc.radius + desc.splat_scale;
            cloud.bounds_min[0] = -b;
            cloud.bounds_min[1] = -b;
            cloud.bounds_min[2] = -b;
            cloud.bounds_max[0] = b;
            cloud.bounds_max[1] = b;
            cloud.bounds_max[2] = b;

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

            GaussianSplatCloudData data{};
            if (!import_ascii_ply_gaussian_splats(bytes->data(), bytes->size(), data)) {
                logger.error("failed to import ASCII PLY gaussian splat cloud");
                return compile_failed_node(input);
            }

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