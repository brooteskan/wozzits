// src/engine/assets/placed_field/placed_field_compilers.cpp

#include <engine/assets/placed_field/placed_field_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/key_factories/placed_field.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets::internal
{
    namespace
    {
        wz::asset::AssetNode compiled_placed_field_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    }

    void register_placed_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        PlacedFieldTable& placed_field_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kPlacedFieldSchema,
            .output_type = kAssetTypePlacedField,
            // Two required ports: the frame-less field first, the Placement
            // frame second. The order matches make_placed_field_key's deps_hash
            // folding order. v1 accepts a scalar field; widen the field port's
            // type to generalise to vector fields / splats (issue #223 Q3).
            .input_ports = {
                { "field", kAssetTypeScalarField },
                { "placement", kAssetTypePlacement },
            },
            // No parameters: a PlacedField is fully determined by its two deps.
            .parameters = {},
            .compile = [&logger, &placed_field_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                // Locate the deps by asset TYPE, not positional index — dep
                // ordering in the DAG is not guaranteed. A PlacedField binds
                // exactly one field + one placement; the first of each wins.
                wz::asset::AssetKey field_key{};
                wz::asset::AssetType field_type = wz::asset::AssetType::Unknown;
                wz::asset::AssetKey placement_key{};
                for (const wz::asset::AssetNode& dep : dep_nodes) {
                    if (dep.type == kAssetTypeScalarField) {
                        if (field_key == wz::asset::AssetKey{}) {
                            field_key = dep.key;
                            field_type = dep.type;
                        }
                    }
                    else if (dep.type == kAssetTypePlacement) {
                        if (placement_key == wz::asset::AssetKey{}) {
                            placement_key = dep.key;
                        }
                    }
                }

                if (field_key == wz::asset::AssetKey{}) {
                    logger.error(
                        "placed field requires a scalar field dependency");
                    return compile_failed_node(
                        input,
                        "placed field requires a scalar field dependency");
                }
                if (placement_key == wz::asset::AssetKey{}) {
                    logger.error(
                        "placed field requires a placement dependency");
                    return compile_failed_node(
                        input,
                        "placed field requires a placement dependency");
                }

                PlacedFieldData data{};
                data.field_key = field_key;
                data.placement_key = placement_key;
                data.field_type = field_type;
                if (!data.valid()) {
                    logger.error("compiled placed field is invalid");
                    return compile_failed_node(
                        input, "compiled placed field is invalid");
                }

                wz::asset::ResourceHandle handle =
                    placed_field_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store placed field");
                    return compile_failed_node(
                        input, "failed to store placed field");
                }

                return compiled_placed_field_node(input, handle);
            }
        });
    }
}
