// src/engine/assets/placement/placement_compilers.cpp

#include <engine/assets/placement/placement_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <any>

namespace wz::engine::assets::internal
{
    namespace
    {
        // Build a PlacementCompileDesc from an authored ParamBlock. Mirrors the
        // ParamBlock-or-typed-desc pattern used by the collision compilers: the
        // graph/editor path supplies a ParamBlock, programmatic callers supply a
        // typed PlacementCompileDesc directly in input.meta.
        PlacementCompileDesc placement_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            PlacementCompileDesc desc{};
            desc.origin[0] =
                static_cast<float>(params.get<double>("origin_x", desc.origin[0]));
            desc.origin[2] =
                static_cast<float>(params.get<double>("origin_z", desc.origin[2]));
            desc.extent[0] =
                static_cast<float>(params.get<double>("extent_x", desc.extent[0]));
            desc.extent[1] =
                static_cast<float>(params.get<double>("extent_y", desc.extent[1]));
            desc.extent[2] =
                static_cast<float>(params.get<double>("extent_z", desc.extent[2]));
            desc.base_height =
                static_cast<float>(
                    params.get<double>("base_height", desc.base_height));
            return desc;
        }

        wz::asset::AssetNode compiled_placement_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    }

    void register_placement_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        PlacementTable& placement_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kPlacementSchema,
            .output_type = kAssetTypePlacement,
            // No input ports: Placement is a pure authored-params source.
            .parameters = {
                {
                    .name = "origin_x",
                    .type = wz::asset::ParamType::Float,
                    .label = "Origin X",
                    .default_num = 0.0,
                },
                {
                    // origin_y is intentionally NOT a separate param: the
                    // frame's vertical origin is base_height (origin[1] mirrors
                    // it), so authoring one value keeps them consistent.
                    .name = "origin_z",
                    .type = wz::asset::ParamType::Float,
                    .label = "Origin Z",
                    .default_num = 0.0,
                },
                {
                    .name = "extent_x",
                    .type = wz::asset::ParamType::Float,
                    .label = "Extent X",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1000000.0,
                },
                {
                    .name = "extent_y",
                    .type = wz::asset::ParamType::Float,
                    .label = "Extent Y (vertical scale)",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1000000.0,
                },
                {
                    .name = "extent_z",
                    .type = wz::asset::ParamType::Float,
                    .label = "Extent Z",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1000000.0,
                },
                {
                    .name = "base_height",
                    .type = wz::asset::ParamType::Float,
                    .label = "Base height",
                    .default_num = 0.0,
                    .min = -1000000.0,
                    .max = 1000000.0,
                },
            },
            .compile = [&logger, &placement_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                PlacementCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<PlacementCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc = placement_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }
                if (!desc) {
                    logger.error(
                        "placement node missing PlacementCompileDesc");
                    return compile_failed_node(input);
                }

                if (!dep_nodes.empty()) {
                    logger.error(
                        "placement should not have dependencies");
                    return compile_failed_node(input);
                }

                PlacementData data{};
                data.origin[0] = desc->origin[0];
                data.origin[1] = desc->base_height;
                data.origin[2] = desc->origin[2];
                data.extent[0] = desc->extent[0];
                data.extent[1] = desc->extent[1];
                data.extent[2] = desc->extent[2];
                data.base_height = desc->base_height;
                if (!data.valid()) {
                    logger.error("compiled placement is invalid");
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle =
                    placement_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store placement");
                    return compile_failed_node(input);
                }

                return compiled_placement_node(input, handle);
            }
        });
    }
}
