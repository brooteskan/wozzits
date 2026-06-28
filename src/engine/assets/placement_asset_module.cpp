// src/engine/assets/placement_asset_module.cpp

#include <engine/assets/placement_asset_module.h>

#include <engine/assets/key_factories/placement.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    PlacementAssetModule::PlacementAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        PlacementTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    PlacementAsset PlacementAssetModule::create_placement(
        const PlacementDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("placement asset has empty name");
            return {};
        }

        PlacementCompileDesc compile_desc{};
        compile_desc.origin[0] = desc.origin[0];
        compile_desc.origin[1] = desc.origin[1];
        compile_desc.origin[2] = desc.origin[2];
        compile_desc.extent[0] = desc.extent[0];
        compile_desc.extent[1] = desc.extent[1];
        compile_desc.extent[2] = desc.extent[2];
        compile_desc.base_height = desc.base_height;

        const wz::asset::AssetKey key =
            make_placement_key(
                desc.name,
                desc.origin,
                desc.extent,
                desc.base_height);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypePlacement;
        node.schema = kPlacementSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        // Placement has no dependencies.
        system_.register_asset(std::move(node));

        return PlacementAsset{ .output = key };
    }

    PlacementHandle PlacementAssetModule::get_placement(
        const PlacementAsset& asset) const
    {
        PlacementHandle out = find_placement(asset);
        if (asset.valid() && !out.valid()) {
            logger_.error("placement asset handle not found");
        }
        return out;
    }

    PlacementHandle PlacementAssetModule::find_placement(
        const PlacementAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        PlacementHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        return out;
    }

    const PlacementData* PlacementAssetModule::get_placement_data(
        PlacementHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }

        return table_.get(handle.handle);
    }
}
