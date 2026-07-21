#include <engine/assets/render_binding_layout/render_binding_layout_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <array>
#include <any>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        // The layout tables are encoded as INDEXED param rows within the
        // existing scalar param model (issue #227 / #226 gap 1): binding0..7,
        // const0..7, sampler0..1. Row presence: a binding/const row exists iff
        // its semantic/name string is non-empty; a sampler row exists iff its
        // kind is not "None". Present rows are packed in index order — that
        // order IS the derived register order (t0,t1…/s0,s1…).

        constexpr std::array<std::string_view, 3> kVisibilityOptions = {
            "All",
            "Vertex",
            "Pixel",
        };

        constexpr std::array<std::string_view, 4> kConstantsHeadOptions = {
            "None",
            "MVP (16)",
            "World + view-proj + camera (36)",
            "Camera-snapped terrain (32)",
        };

        // RenderBindingViewHead — the VIEW-frequency block at register space 0
        // (a8d3450): the observer plus the atmosphere it looks through, filled
        // once per frame by the renderer rather than per draw. Option index IS
        // the enum value, so "None" must stay first — that is the value every
        // layout authored before this param existed decodes to, and it derives
        // byte-identically to one with no view block at all.
        constexpr std::array<std::string_view, 3> kViewHeadOptions = {
            "None",
            "Camera + fog (12)",
            "Screen (4)",   // viewport size for 2D overlays (RenderBindingViewHead::Screen)
        };

        constexpr std::array<std::string_view, 5> kConstantTypeOptions = {
            "Float",
            "Float2",
            "Float3",
            "Float4",
            "Color",
        };

        constexpr std::array<std::string_view, 2> kBindingKindOptions = {
            "Texture SRV",
            "Structured SRV",
        };

        // Option 0 marks an absent sampler row; values 1.. map to
        // StaticSamplerKind (value - 1).
        constexpr std::array<std::string_view, 3> kSamplerKindOptions = {
            "None",
            "Linear clamp",
            "Linear wrap",
        };

        // Indexed param names. ParamDecl/ParamBlock reference string_views with
        // static storage, so every row name is spelled out.
        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutConstantFields> kConstNameParams = {
            "const0_name", "const1_name", "const2_name", "const3_name",
            "const4_name", "const5_name", "const6_name", "const7_name",
        };
        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutConstantFields> kConstTypeParams = {
            "const0_type", "const1_type", "const2_type", "const3_type",
            "const4_type", "const5_type", "const6_type", "const7_type",
        };
        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutBindings> kBindingSemanticParams = {
            "binding0_semantic", "binding1_semantic",
            "binding2_semantic", "binding3_semantic",
            "binding4_semantic", "binding5_semantic",
            "binding6_semantic", "binding7_semantic",
        };
        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutBindings> kBindingKindParams = {
            "binding0_kind", "binding1_kind", "binding2_kind", "binding3_kind",
            "binding4_kind", "binding5_kind", "binding6_kind", "binding7_kind",
        };
        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutBindings> kBindingVisibilityParams = {
            "binding0_visibility", "binding1_visibility",
            "binding2_visibility", "binding3_visibility",
            "binding4_visibility", "binding5_visibility",
            "binding6_visibility", "binding7_visibility",
        };
        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutSamplers> kSamplerKindParams = {
            "sampler0_kind", "sampler1_kind",
        };
        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutSamplers> kSamplerVisibilityParams = {
            "sampler0_visibility", "sampler1_visibility",
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

        RenderBindingLayoutCompileDesc render_binding_layout_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            RenderBindingLayoutCompileDesc desc{};
            RenderBindingLayoutData& layout = desc.layout;

            layout.constants_semantic =
                params.get<std::string>("constants_semantic", {});
            layout.constants_visibility =
                enum_param(
                    params,
                    "constants_visibility",
                    layout.constants_visibility,
                    kVisibilityOptions);
            layout.constants_head =
                enum_param(
                    params,
                    "constants_head",
                    layout.constants_head,
                    kConstantsHeadOptions);
            layout.constants_dwords =
                params.get<uint32_t>(
                    "constants_dwords",
                    layout.constants_dwords);

            // The view block is head-only (no semantic, no tail, no authored
            // size): the renderer owns every dword in it, so this one dial is
            // the whole authored surface.
            layout.view_head =
                enum_param(
                    params,
                    "view_head",
                    layout.view_head,
                    kViewHeadOptions);

            for (uint32_t i = 0;
                 i < kMaxRenderBindingLayoutConstantFields;
                 ++i)
            {
                RenderBindingConstantField field{};
                field.name =
                    params.get<std::string>(kConstNameParams[i], {});
                if (field.name.empty()) {
                    continue;
                }
                field.type =
                    enum_param(
                        params,
                        kConstTypeParams[i],
                        field.type,
                        kConstantTypeOptions);
                layout.constant_fields.push_back(std::move(field));
            }

            for (uint32_t i = 0; i < kMaxRenderBindingLayoutBindings; ++i) {
                RenderBindingRow row{};
                row.semantic =
                    params.get<std::string>(kBindingSemanticParams[i], {});
                if (row.semantic.empty()) {
                    continue;
                }
                row.kind =
                    enum_param(
                        params,
                        kBindingKindParams[i],
                        row.kind,
                        kBindingKindOptions);
                row.visibility =
                    enum_param(
                        params,
                        kBindingVisibilityParams[i],
                        row.visibility,
                        kVisibilityOptions);
                layout.bindings.push_back(std::move(row));
            }

            for (uint32_t i = 0; i < kMaxRenderBindingLayoutSamplers; ++i) {
                const int64_t kind =
                    params.get<int64_t>(kSamplerKindParams[i], 0);
                if (kind <= 0
                    || kind >= static_cast<int64_t>(
                        kSamplerKindOptions.size()))
                {
                    continue;
                }
                RenderBindingSamplerRow row{};
                row.kind = static_cast<StaticSamplerKind>(kind - 1);
                row.visibility =
                    enum_param(
                        params,
                        kSamplerVisibilityParams[i],
                        row.visibility,
                        kVisibilityOptions);
                layout.samplers.push_back(row);
            }

            return desc;
        }

        std::vector<wz::asset::ParamDecl> make_layout_parameters()
        {
            using wz::asset::ParamDecl;
            using wz::asset::ParamType;

            std::vector<ParamDecl> parameters;
            parameters.push_back({
                .name = "name",
                .type = ParamType::String,
                .label = "Name",
            });
            parameters.push_back({
                .name = "constants_semantic",
                .type = ParamType::String,
                .label = "Constants semantic",
            });
            parameters.push_back({
                .name = "constants_dwords",
                .type = ParamType::Int,
                .label = "Constants dwords (0 = derived)",
                .default_num = 0,
                .min = 0,
                .max = 64,
            });
            parameters.push_back({
                .name = "constants_visibility",
                .type = ParamType::Enum,
                .label = "Constants visibility",
                .default_num =
                    static_cast<double>(ShaderVisibility::All),
                .options = kVisibilityOptions,
            });
            parameters.push_back({
                .name = "constants_head",
                .type = ParamType::Enum,
                .label = "Constants head",
                .default_num =
                    static_cast<double>(RenderBindingConstantsHead::None),
                .options = kConstantsHeadOptions,
            });
            parameters.push_back({
                .name = "view_head",
                .type = ParamType::Enum,
                .label = "View head",
                .default_num =
                    static_cast<double>(RenderBindingViewHead::None),
                .options = kViewHeadOptions,
            });
            for (uint32_t i = 0;
                 i < kMaxRenderBindingLayoutConstantFields;
                 ++i)
            {
                parameters.push_back({
                    .name = kConstNameParams[i],
                    .type = ParamType::String,
                    .label = kConstNameParams[i],
                });
                parameters.push_back({
                    .name = kConstTypeParams[i],
                    .type = ParamType::Enum,
                    .label = kConstTypeParams[i],
                    .default_num =
                        static_cast<double>(RenderBindingConstantType::Float),
                    .options = kConstantTypeOptions,
                });
            }
            for (uint32_t i = 0; i < kMaxRenderBindingLayoutBindings; ++i) {
                parameters.push_back({
                    .name = kBindingSemanticParams[i],
                    .type = ParamType::String,
                    .label = kBindingSemanticParams[i],
                });
                parameters.push_back({
                    .name = kBindingKindParams[i],
                    .type = ParamType::Enum,
                    .label = kBindingKindParams[i],
                    .default_num =
                        static_cast<double>(RenderBindingKind::TextureSrv),
                    .options = kBindingKindOptions,
                });
                parameters.push_back({
                    .name = kBindingVisibilityParams[i],
                    .type = ParamType::Enum,
                    .label = kBindingVisibilityParams[i],
                    .default_num =
                        static_cast<double>(ShaderVisibility::All),
                    .options = kVisibilityOptions,
                });
            }
            for (uint32_t i = 0; i < kMaxRenderBindingLayoutSamplers; ++i) {
                parameters.push_back({
                    .name = kSamplerKindParams[i],
                    .type = ParamType::Enum,
                    .label = kSamplerKindParams[i],
                    .default_num = 0.0,
                    .options = kSamplerKindOptions,
                });
                parameters.push_back({
                    .name = kSamplerVisibilityParams[i],
                    .type = ParamType::Enum,
                    .label = kSamplerVisibilityParams[i],
                    .default_num =
                        static_cast<double>(ShaderVisibility::All),
                    .options = kVisibilityOptions,
                });
            }
            return parameters;
        }
    }

    void register_render_binding_layout_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        RenderBindingLayoutTable& table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kRenderBindingLayoutSchema,
            .output_type = kAssetTypeRenderBindingLayout,
            .parameters = make_layout_parameters(),
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                RenderBindingLayoutCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<RenderBindingLayoutCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            render_binding_layout_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }

                if (!desc) {
                    logger.error(
                        "render binding layout missing compile desc");
                    return compile_failed_node(
                        input,
                        "render binding layout missing compile desc");
                }

                if (!dep_handles.empty()) {
                    logger.error(
                        "render binding layout must not have dependencies");
                    return compile_failed_node(
                        input,
                        "render binding layout must not have dependencies");
                }

                // Validate through the SRG derivation so the failure reason
                // names the offending row instead of a bare "invalid".
                RenderBindingLayoutSrg srg{};
                std::string error;
                if (!build_render_binding_layout_srg(
                        desc->layout, srg, error))
                {
                    logger.error("render binding layout: " + error);
                    return compile_failed_node(input, std::move(error));
                }

                const wz::asset::ResourceHandle handle =
                    table.add(desc->layout);
                if (!handle.valid()) {
                    logger.error("failed to store render binding layout");
                    return compile_failed_node(
                        input,
                        "failed to store render binding layout");
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });

        // ── HLSL binding prelude (issue #231) ────────────────────────────────
        //
        // The shader-side twin of the SRG derivation above: prepends the
        // layout's generated declarations to an authored HLSL body and outputs
        // the combined ShaderSource a shader node compiles. Routing the
        // include THROUGH the DAG (instead of a #include resolved by the HLSL
        // compiler) keeps it key-visible: a layout edit re-keys this node, the
        // shader, and the program, so the declarations and the root signature
        // can never drift apart via a stale cache.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kHLSLBindingPreludeSchema,
            .output_type = wz::asset::AssetType::ShaderSource,
            .input_ports = {
                { "source", wz::asset::AssetType::ShaderSource },
                { "binding_layout", kAssetTypeRenderBindingLayout },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
            },
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                // Dispatch deps by compiled payload rather than port index:
                // the body arrives as ShaderSource bytes, the layout as a
                // table handle — unambiguous regardless of dep order.
                const std::vector<uint8_t>* body = nullptr;
                const RenderBindingLayoutData* layout = nullptr;
                for (const wz::asset::AssetNode& dep : dep_nodes) {
                    if (const auto* bytes =
                            std::get_if<std::vector<uint8_t>>(&dep.payload))
                    {
                        body = bytes;
                        continue;
                    }
                    if (const auto* handle =
                            std::get_if<wz::asset::ResourceHandle>(
                                &dep.payload))
                    {
                        layout = table.get(*handle);
                    }
                }

                if (!body) {
                    logger.error(
                        "hlsl binding prelude: shader source dependency did "
                        "not resolve");
                    return compile_failed_node(
                        input,
                        "shader source dependency did not resolve");
                }
                if (!layout) {
                    logger.error(
                        "hlsl binding prelude: binding layout dependency did "
                        "not resolve");
                    return compile_failed_node(
                        input,
                        "binding layout dependency did not resolve");
                }

                std::string prelude;
                std::string error;
                if (!generate_hlsl_binding_prelude(*layout, prelude, error)) {
                    logger.error("hlsl binding prelude: " + error);
                    return compile_failed_node(input, std::move(error));
                }
                prelude += '\n';

                std::vector<uint8_t> combined;
                combined.reserve(prelude.size() + body->size());
                combined.insert(
                    combined.end(), prelude.begin(), prelude.end());
                combined.insert(combined.end(), body->begin(), body->end());

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = std::move(combined);
                return out;
            }
        });
    }
}
