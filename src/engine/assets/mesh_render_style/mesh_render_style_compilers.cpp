#include <engine/assets/mesh_render_style/mesh_render_style_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <array>
#include <any>
#include <span>
#include <string_view>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr std::array<std::string_view, 3>
            kFieldPaletteOptions = {
                "Heat",
                "Grayscale",
                "Diverging",
            };

        constexpr std::array<std::string_view, 2> kMaskDomainOptions = {
            "Face",
            "Vertex",
        };

        constexpr std::array<std::string_view, 2> kMaskOverlapOptions = {
            "Priority",
            "Alpha blend",
        };

        template<class Enum, std::size_t Count>
        Enum enum_param(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            Enum fallback,
            const std::array<std::string_view, Count>&)
        {
            const int64_t value =
                params.get<int64_t>(name, static_cast<int64_t>(fallback));
            if (value >= 0 && value < static_cast<int64_t>(Count)) {
                return static_cast<Enum>(value);
            }
            return fallback;
        }

        void assign_color(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            std::string_view alpha_name,
            float color[4])
        {
            const auto rgb =
                params.get<std::array<float, 3>>(
                    name,
                    { color[0], color[1], color[2] });
            color[0] = rgb[0];
            color[1] = rgb[1];
            color[2] = rgb[2];
            color[3] = params.get<float>(alpha_name, color[3]);
        }

        MeshRenderStyleCompileDesc mesh_render_style_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            MeshRenderStyleCompileDesc desc{};
            MeshRenderStyleData& style = desc.style;

            style.wireframe.enabled =
                params.get<bool>(
                    "wireframe_enabled",
                    style.wireframe.enabled);
            assign_color(
                params,
                "wireframe_color",
                "wireframe_alpha",
                style.wireframe.color);
            style.wireframe.emissive_strength =
                params.get<float>(
                    "wireframe_emissive_strength",
                    style.wireframe.emissive_strength);

            style.surface.enabled =
                params.get<bool>("surface_enabled", style.surface.enabled);
            assign_color(
                params,
                "surface_color",
                "surface_alpha",
                style.surface.color);
            style.surface.emissive_strength =
                params.get<float>(
                    "surface_emissive_strength",
                    style.surface.emissive_strength);

            style.alpha = params.get<float>("alpha", style.alpha);
            style.depth_test =
                params.get<bool>("depth_test", style.depth_test);
            style.depth_write =
                params.get<bool>("depth_write", style.depth_write);
            style.double_sided =
                params.get<bool>("double_sided", style.double_sided);
            style.hidden_line_prepass =
                params.get<bool>(
                    "hidden_line_prepass",
                    style.hidden_line_prepass);

            MeshFieldVisualizationStyle& field = style.field_visualization;
            field.enabled =
                params.get<bool>("field_enabled", field.enabled);
            field.channel_id =
                params.get<uint32_t>("field_channel_id", field.channel_id);
            field.value_min =
                params.get<float>("field_value_min", field.value_min);
            field.value_max =
                params.get<float>("field_value_max", field.value_max);
            field.gamma =
                params.get<float>("field_gamma", field.gamma);
            field.palette =
                enum_param(
                    params,
                    "field_palette",
                    field.palette,
                    kFieldPaletteOptions);

            MeshMaskRenderStyleData& mask = style.mask;
            mask.enabled = params.get<bool>("mask_enabled", mask.enabled);
            mask.domain =
                enum_param(
                    params,
                    "mask_domain",
                    mask.domain,
                    kMaskDomainOptions);
            mask.overlap_mode =
                enum_param(
                    params,
                    "mask_overlap_mode",
                    mask.overlap_mode,
                    kMaskOverlapOptions);
            assign_color(
                params,
                "mask_unmatched_color",
                "mask_unmatched_alpha",
                mask.unmatched_color);
            mask.show_unmatched =
                params.get<bool>(
                    "mask_show_unmatched",
                    mask.show_unmatched);

            MeshMaskRule rule{};
            rule.enabled =
                params.get<bool>("mask_rule_enabled", rule.enabled);
            rule.input_channel_id =
                params.get<uint32_t>(
                    "mask_rule_input_channel_id",
                    rule.input_channel_id);
            rule.lo = params.get<float>("mask_rule_lo", rule.lo);
            rule.hi = params.get<float>("mask_rule_hi", rule.hi);
            assign_color(
                params,
                "mask_rule_color",
                "mask_rule_alpha",
                rule.color);
            rule.priority =
                params.get<int32_t>("mask_rule_priority", rule.priority);
            if (mask.enabled && rule.enabled) {
                mask.rules.push_back(rule);
            }

            return desc;
        }
    }

    void register_mesh_render_style_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshRenderStyleTable& table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshRenderStyleSchema,
            .output_type = kAssetTypeMeshRenderStyle,
            .parameters = {
                {
                    .name = "wireframe_enabled",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Wireframe",
                    .default_num = 1.0,
                },
                {
                    .name = "wireframe_color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Wireframe color",
                },
                {
                    .name = "wireframe_alpha",
                    .type = wz::asset::ParamType::Float,
                    .label = "Wireframe alpha",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "wireframe_emissive_strength",
                    .type = wz::asset::ParamType::Float,
                    .label = "Wireframe emissive",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "surface_enabled",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Surface",
                    .default_num = 0.0,
                },
                {
                    .name = "surface_color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Surface color",
                },
                {
                    .name = "surface_alpha",
                    .type = wz::asset::ParamType::Float,
                    .label = "Surface alpha",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "surface_emissive_strength",
                    .type = wz::asset::ParamType::Float,
                    .label = "Surface emissive",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "alpha",
                    .type = wz::asset::ParamType::Float,
                    .label = "Alpha",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "depth_test",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Depth test",
                    .default_num = 1.0,
                },
                {
                    .name = "depth_write",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Depth write",
                    .default_num = 0.0,
                },
                {
                    .name = "double_sided",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Double sided",
                    .default_num = 1.0,
                },
                {
                    .name = "hidden_line_prepass",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Hidden line prepass",
                    .default_num = 1.0,
                },
                {
                    .name = "field_enabled",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Field visualization",
                    .default_num = 0.0,
                },
                {
                    .name = "field_channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Field channel",
                    .default_num = 0,
                    .min = 0,
                    .max = 65535,
                },
                {
                    .name = "field_value_min",
                    .type = wz::asset::ParamType::Float,
                    .label = "Field min",
                    .default_num = 0.0,
                },
                {
                    .name = "field_value_max",
                    .type = wz::asset::ParamType::Float,
                    .label = "Field max",
                    .default_num = 1.0,
                },
                {
                    .name = "field_gamma",
                    .type = wz::asset::ParamType::Float,
                    .label = "Field gamma",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 8.0,
                },
                {
                    .name = "field_palette",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Field palette",
                    .default_num =
                        static_cast<double>(
                            MeshFieldVisualizationPalette::Heat),
                    .options = kFieldPaletteOptions,
                },
                {
                    .name = "mask_enabled",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Mask",
                    .default_num = 0.0,
                },
                {
                    .name = "mask_domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Mask domain",
                    .default_num =
                        static_cast<double>(MeshMaskDomain::Face),
                    .options = kMaskDomainOptions,
                },
                {
                    .name = "mask_overlap_mode",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Mask overlap",
                    .default_num =
                        static_cast<double>(MeshMaskOverlapMode::Priority),
                    .options = kMaskOverlapOptions,
                },
                {
                    .name = "mask_unmatched_color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Unmatched color",
                },
                {
                    .name = "mask_unmatched_alpha",
                    .type = wz::asset::ParamType::Float,
                    .label = "Unmatched alpha",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "mask_show_unmatched",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Show unmatched",
                    .default_num = 1.0,
                },
                {
                    .name = "mask_rule_enabled",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Mask rule",
                    .default_num = 1.0,
                },
                {
                    .name = "mask_rule_input_channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Rule channel",
                    .default_num = 0,
                    .min = 0,
                    .max = 65535,
                },
                {
                    .name = "mask_rule_lo",
                    .type = wz::asset::ParamType::Float,
                    .label = "Rule lo",
                    .default_num = 0.0,
                },
                {
                    .name = "mask_rule_hi",
                    .type = wz::asset::ParamType::Float,
                    .label = "Rule hi",
                    .default_num = 1.0,
                },
                {
                    .name = "mask_rule_color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Rule color",
                },
                {
                    .name = "mask_rule_alpha",
                    .type = wz::asset::ParamType::Float,
                    .label = "Rule alpha",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "mask_rule_priority",
                    .type = wz::asset::ParamType::Int,
                    .label = "Rule priority",
                    .default_num = 0,
                },
            },
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                MeshRenderStyleCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<MeshRenderStyleCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            mesh_render_style_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }

                if (!desc) {
                    logger.error("mesh render style missing compile desc");
                    return compile_failed_node(input);
                }

                if (!dep_handles.empty()) {
                    logger.error("mesh render style must not have dependencies");
                    return compile_failed_node(input);
                }

                if (!desc->style.valid()) {
                    logger.error("mesh render style is invalid");
                    return compile_failed_node(input);
                }

                const wz::asset::ResourceHandle handle =
                    table.add(desc->style);
                if (!handle.valid()) {
                    logger.error("failed to store mesh render style");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });
    }
}
