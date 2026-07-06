// src/engine/assets/placed_field_asset_module.cpp

#include <engine/assets/placed_field_asset_module.h>

#include <engine/assets/key_factories/placed_field.h>
#include <engine/assets/schema_ids.h>

#include <vector>

namespace wz::engine::assets
{
    PlacedFieldAssetModule::PlacedFieldAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        PlacedFieldTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    PlacedFieldAsset PlacedFieldAssetModule::create_placed_field(
        const PlacedFieldDesc& desc)
    {
        if (desc.field_key == wz::asset::AssetKey{}) {
            logger_.error("placed field asset has no field dependency");
            return {};
        }
        if (desc.placement_key == wz::asset::AssetKey{}) {
            logger_.error("placed field asset has no placement dependency");
            return {};
        }

        const wz::asset::AssetKey key =
            make_placed_field_key(
                desc.field_key,
                desc.placement_key,
                desc.field_type);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypePlacedField;
        node.schema = kPlacedFieldSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};

        // Deps in field-first, placement-second order — matching the compiler's
        // read order and make_placed_field_key's deps_hash folding order.
        system_.register_asset(
            std::move(node),
            { desc.field_key, desc.placement_key });

        return PlacedFieldAsset{ .output = key };
    }

    PlacedFieldHandle PlacedFieldAssetModule::get_placed_field(
        const PlacedFieldAsset& asset) const
    {
        PlacedFieldHandle out = find_placed_field(asset);
        if (asset.valid() && !out.valid()) {
            logger_.error("placed field asset handle not found");
        }
        return out;
    }

    PlacedFieldHandle PlacedFieldAssetModule::find_placed_field(
        const PlacedFieldAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        PlacedFieldHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        return out;
    }

    const PlacedFieldData* PlacedFieldAssetModule::get_placed_field_data(
        PlacedFieldHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }

        return table_.get(handle.handle);
    }
}
