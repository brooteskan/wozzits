// src/engine/assets/gaussian_splat/gaussian_splat_compilers.cpp

#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_ply_importer.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <limits>
#include <span>
#include <string>
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

        ProceduralGaussianSplatCloudCompileDesc
        procedural_gaussian_splat_cloud_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            ProceduralGaussianSplatCloudCompileDesc desc{};
            desc.count = params.get<uint32_t>("count", desc.count);
            desc.radius = params.get<float>("radius", desc.radius);
            desc.splat_scale =
                params.get<float>("splat_scale", desc.splat_scale);
            return desc;
        }

        GaussianSplatFromScalarFieldCompileDesc
        gaussian_splat_from_scalar_field_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            GaussianSplatFromScalarFieldCompileDesc desc{};
            desc.height_scale =
                params.get<float>("height_scale", desc.height_scale);
            desc.step_x = params.get<float>("step_x", desc.step_x);
            desc.step_z = params.get<float>("step_z", desc.step_z);
            desc.splat_scale =
                params.get<float>("splat_scale", desc.splat_scale);
            desc.opacity = params.get<float>("opacity", desc.opacity);
            desc.normalize_values =
                params.get<bool>(
                    "normalize_values",
                    desc.normalize_values);
            desc.use_threshold =
                params.get<bool>("use_threshold", desc.use_threshold);
            desc.emit_threshold =
                params.get<float>("emit_threshold", desc.emit_threshold);
            return desc;
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
        GaussianSplatCloudData make_splat_cloud_from_scalar_field(
            const GaussianSplatFromScalarFieldCompileDesc& desc,
            const ScalarFieldData& field,
            wz::Logger& /*logger*/)
        {
            constexpr float SH_C0 = 0.28209479177387814f;
            constexpr float kInf  = std::numeric_limits<float>::max();

            const float log_scale = std::log(std::max(desc.splat_scale, 1e-6f));

            const float logit_opacity = [&] {
                const float p = std::max(0.0001f, std::min(0.9999f, desc.opacity));
                return std::log(p / (1.0f - p));
            }();

            const float value_range  = field.max_value - field.min_value;
            const bool can_normalize = (value_range > 1e-12f);

            // Center the heightfield grid on the origin.
            // field.width columns along world X; field.height rows along world Z.
            const float half_x = 0.5f * static_cast<float>(field.width  - 1) * desc.step_x;
            const float half_z = 0.5f * static_cast<float>(field.height - 1) * desc.step_z;

            GaussianSplatCloudData cloud{};
            cloud.splats.reserve(field.width * field.height);

            // Tight bounds accumulated only from emitted splats.
            float bmin[3] = { kInf,  kInf,  kInf};
            float bmax[3] = {-kInf, -kInf, -kInf};

            for (uint32_t iy = 0; iy < field.height; ++iy) {
                for (uint32_t ix = 0; ix < field.width; ++ix) {
                    const float raw = field.at(ix, iy);

                    const float normalized =
                        (desc.normalize_values && can_normalize)
                        ? (raw - field.min_value) / value_range
                        : raw;

                    if (desc.use_threshold && normalized < desc.emit_threshold)
                        continue;

                    const float wx = static_cast<float>(ix) * desc.step_x - half_x;
                    const float wy = normalized * desc.height_scale;
                    const float wz = static_cast<float>(iy) * desc.step_z - half_z;

                    GaussianSplat splat{};
                    splat.position[0] = wx;
                    splat.position[1] = wy;
                    splat.position[2] = wz;

                    splat.scale[0] = log_scale;
                    splat.scale[1] = log_scale;
                    splat.scale[2] = log_scale;

                    // Identity quaternion.
                    splat.rotation[0] = 1.0f;

                    splat.opacity = logit_opacity;

                    // Grayscale color derived from normalized value.
                    const float display = std::max(0.0f, std::min(1.0f, normalized));
                    const float sh_dc = (display - 0.5f) / SH_C0;
                    splat.color_dc[0] = sh_dc;
                    splat.color_dc[1] = sh_dc;
                    splat.color_dc[2] = sh_dc;

                    bmin[0] = std::min(bmin[0], wx);
                    bmin[1] = std::min(bmin[1], wy);
                    bmin[2] = std::min(bmin[2], wz);
                    bmax[0] = std::max(bmax[0], wx);
                    bmax[1] = std::max(bmax[1], wy);
                    bmax[2] = std::max(bmax[2], wz);

                    cloud.splats.push_back(splat);
                }
            }

            if (cloud.splats.empty())
                return {};

            const float r = desc.splat_scale;
            cloud.bounds.min[0] = bmin[0] - r;
            cloud.bounds.min[1] = bmin[1] - r;
            cloud.bounds.min[2] = bmin[2] - r;
            cloud.bounds.max[0] = bmax[0] + r;
            cloud.bounds.max[1] = bmax[1] + r;
            cloud.bounds.max[2] = bmax[2] + r;
            cloud.bounds.valid  = true;

            cloud.opacity_min = logit_opacity;
            cloud.opacity_max = logit_opacity;
            cloud.scale_min   = log_scale;
            cloud.scale_max   = log_scale;
            cloud.f_rest_count = 0;

            return cloud;
        }

    } // anonymous namespace

    void register_gaussian_splat_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& table,
        ScalarFieldTable& scalar_field_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kProceduralGaussianSplatCloudSchema,
            .output_type = kAssetTypeGaussianSplatCloud,
            .parameters = {
                {
                    .name = "count",
                    .type = wz::asset::ParamType::Int,
                    .label = "Count",
                    .default_num = 0,
                    .min = 0,
                    .max = 1000000,
                },
                {
                    .name = "radius",
                    .type = wz::asset::ParamType::Float,
                    .label = "Radius",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "splat_scale",
                    .type = wz::asset::ParamType::Float,
                    .label = "Splat scale",
                    .default_num = 0.05,
                    .min = 0.0,
                    .max = 10.0,
                },
            },
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                ProceduralGaussianSplatCloudCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<ProceduralGaussianSplatCloudCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        param_desc =
                            procedural_gaussian_splat_cloud_desc_from_params(
                                *params);
                        desc = &param_desc;
                    }
                }

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
            .input_ports = {
                { "source_file", kAssetTypeRawFile },
            },
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_ply_gaussian_splat_cloud_node(
                    input, dep_nodes, logger, table);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGaussianSplatFromFieldSchema,
            .output_type = kAssetTypeGaussianSplatCloud,
            .input_ports = {
                { "scalar_field", kAssetTypeScalarField },
            },
            .parameters = {
                {
                    .name = "height_scale",
                    .type = wz::asset::ParamType::Float,
                    .label = "Height scale",
                    .default_num = 1.0,
                    .min = -100.0,
                    .max = 100.0,
                },
                {
                    .name = "step_x",
                    .type = wz::asset::ParamType::Float,
                    .label = "Step X",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "step_z",
                    .type = wz::asset::ParamType::Float,
                    .label = "Step Z",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "splat_scale",
                    .type = wz::asset::ParamType::Float,
                    .label = "Splat scale",
                    .default_num = 0.05,
                    .min = 0.0,
                    .max = 10.0,
                },
                {
                    .name = "opacity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Opacity",
                    .default_num = 0.9,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "normalize_values",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Normalize values",
                    .default_num = 1.0,
                },
                {
                    .name = "use_threshold",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Use threshold",
                    .default_num = 0.0,
                },
                {
                    .name = "emit_threshold",
                    .type = wz::asset::ParamType::Float,
                    .label = "Emit threshold",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 1.0,
                },
            },
            .compile = [&logger, &table, &scalar_field_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> /*dep_nodes*/,
                std::span<const wz::asset::ResourceHandle> dep_handles) -> wz::asset::AssetNode
            {
                GaussianSplatFromScalarFieldCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<GaussianSplatFromScalarFieldCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        param_desc =
                            gaussian_splat_from_scalar_field_desc_from_params(
                                *params);
                        desc = &param_desc;
                    }
                }

                if (!desc) {
                    logger.error("gaussian splat from scalar field missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 1) {
                    logger.error("gaussian splat from scalar field expects exactly one compiled dependency");
                    return compile_failed_node(input);
                }

                const ScalarFieldData* field = scalar_field_table.get(dep_handles[0]);
                if (!field) {
                    logger.error("gaussian splat from scalar field: scalar field not found");
                    return compile_failed_node(input);
                }

                GaussianSplatCloudData data =
                    make_splat_cloud_from_scalar_field(*desc, *field, logger);

                if (!data.valid()) {
                    logger.error("gaussian splat from scalar field: produced empty or invalid cloud");
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle = table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("gaussian splat from scalar field: failed to store cloud");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage   = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }
}
