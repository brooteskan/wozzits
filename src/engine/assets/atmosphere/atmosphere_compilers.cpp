// src/engine/assets/atmosphere/atmosphere_compilers.cpp

#include <engine/assets/atmosphere/atmosphere_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <any>
#include <array>
#include <string_view>

namespace wz::engine::assets::internal
{
    namespace
    {
        // A ParamType::Color value is stored in the ParamBlock variant as a
        // std::array<float,3> — the same storage Float3 uses; Color only changes
        // the widget the editor offers. So a colour dial is read as one vector,
        // not as three scalars. Same helper the light compilers use.
        std::array<float, 3> param_float3(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            const float fallback[3])
        {
            return params.get<std::array<float, 3>>(
                name,
                { fallback[0], fallback[1], fallback[2] });
        }

        // Build an AtmosphereCompileDesc from an authored ParamBlock. Mirrors the
        // ParamBlock-or-typed-desc pattern the placement compiler uses: the
        // graph/editor path supplies a ParamBlock, programmatic callers supply a
        // typed AtmosphereCompileDesc directly in input.meta.
        AtmosphereCompileDesc atmosphere_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            AtmosphereCompileDesc desc{};
            const auto color = param_float3(params, "fog_color", desc.fog_color);
            desc.fog_color[0] = color[0];
            desc.fog_color[1] = color[1];
            desc.fog_color[2] = color[2];
            desc.fog_density =
                params.get<float>("fog_density", desc.fog_density);
            desc.fog_start_distance =
                params.get<float>(
                    "fog_start_distance", desc.fog_start_distance);
            desc.fog_height_falloff =
                params.get<float>(
                    "fog_height_falloff", desc.fog_height_falloff);
            desc.fog_enabled =
                params.get<bool>("fog_enabled", desc.fog_enabled);
            desc.fog_saturation =
                params.get<float>("fog_saturation", desc.fog_saturation);
            return desc;
        }

        wz::asset::AssetNode compiled_atmosphere_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    }

    void register_atmosphere_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AtmosphereTable& atmosphere_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kAtmosphereSchema,
            .output_type = kAssetTypeAtmosphere,
            // No input ports: Atmosphere is a pure authored-params source, like
            // Placement. Fog is authored, never derived from upstream data.
            //
            // A compiler reading params through pb.get<T> MUST declare them here
            // or the editor stores them as strings, get<T> ignores strings, and
            // the dials go silently dead.
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    // Color rather than Float3: identical storage, but the
                    // editor offers a swatch instead of three spinners.
                    //
                    // ParamDecl broadcasts default_num to all three channels
                    // (param_defaults.h), so only a GREY default is expressible
                    // here. AtmosphereData's default is that same grey on
                    // purpose: a graph-authored node and a typed one must not
                    // start from different colours, or one asset means two
                    // things depending on which path built it.
                    .name = "fog_color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Fog color",
                    .default_num = 0.7,
                },
                {
                    .name = "fog_density",
                    .type = wz::asset::ParamType::Float,
                    .label = "Fog density",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "fog_start_distance",
                    .type = wz::asset::ParamType::Float,
                    .label = "Fog start distance",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 100000.0,
                },
                {
                    .name = "fog_height_falloff",
                    .type = wz::asset::ParamType::Float,
                    .label = "Fog height falloff",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "fog_enabled",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Fog enabled",
                },
                {
                    // Colourfulness of the sky the haze scatters: 0 greyscale,
                    // 1 the sky's own colour, up to 4 for a vivid horizon glow.
                    // Reaches the haze through fog_params.w.
                    .name = "fog_saturation",
                    .type = wz::asset::ParamType::Float,
                    .label = "Fog saturation",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 4.0,
                },
            },
            .compile = [&logger, &atmosphere_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                // Typed meta for programmatic callers, ParamBlock for the
                // graph/editor path — the same two-way read the placement
                // compiler uses. Neither present leaves the struct's declared
                // defaults, which describe an atmosphere with the fog off.
                AtmosphereCompileDesc desc{};
                if (const auto* typed =
                        std::any_cast<AtmosphereCompileDesc>(&input.meta))
                {
                    desc = *typed;
                }
                else if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    desc = atmosphere_desc_from_params(*params);
                }

                if (!dep_nodes.empty()) {
                    logger.error("atmosphere should not have dependencies");
                    return compile_failed_node(
                        input, "atmosphere should not have dependencies");
                }

                AtmosphereData data{};
                data.fog_color[0] = desc.fog_color[0];
                data.fog_color[1] = desc.fog_color[1];
                data.fog_color[2] = desc.fog_color[2];
                data.fog_density = desc.fog_density;
                data.fog_start_distance = desc.fog_start_distance;
                data.fog_height_falloff = desc.fog_height_falloff;
                data.fog_enabled = desc.fog_enabled;
                data.fog_saturation = desc.fog_saturation;
                if (!data.valid()) {
                    logger.error("compiled atmosphere is invalid");
                    return compile_failed_node(
                        input, "compiled atmosphere is invalid");
                }

                wz::asset::ResourceHandle handle =
                    atmosphere_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store atmosphere");
                    return compile_failed_node(
                        input, "failed to store atmosphere");
                }

                return compiled_atmosphere_node(input, handle);
            }
        });
    }
}
