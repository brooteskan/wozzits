#include <engine/assets/terrain_visual_proxy_asset_module.h>

#include <engine/assets/key_factories/terrain_visual_proxy.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    TerrainVisualProxyAssetModule::TerrainVisualProxyAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        TerrainVisualProxyTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    TerrainVisualProxyAsset TerrainVisualProxyAssetModule::create_from_terrain(
        const TerrainVisualProxyDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("terrain visual proxy has empty name");
            return {};
        }
        if (!desc.terrain.valid()) {
            logger_.error("terrain visual proxy has invalid terrain: "
                + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_terrain_visual_proxy_key(desc.name, desc.terrain.output);
        if (system_.is_registered(key)) {
            return TerrainVisualProxyAsset{ .output = key };
        }

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeTerrainVisualProxy;
        node.schema = kTerrainVisualProxySchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = desc.terrain.output;

        if (!system_.register_asset(std::move(node), { desc.terrain.output })) {
            logger_.error("terrain visual proxy registration failed: "
                + desc.name);
            return {};
        }

        return TerrainVisualProxyAsset{ .output = key };
    }

    TerrainVisualProxyHandle TerrainVisualProxyAssetModule::get_proxy(
        const TerrainVisualProxyAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        TerrainVisualProxyHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        if (!out.valid()) {
            logger_.error("terrain visual proxy handle not found");
        }
        return out;
    }

    const TerrainVisualProxyData*
    TerrainVisualProxyAssetModule::get_proxy_data(
        TerrainVisualProxyHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        return table_.get(handle.handle);
    }
}
