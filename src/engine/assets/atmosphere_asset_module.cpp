// src/engine/assets/atmosphere_asset_module.cpp

#include <engine/assets/atmosphere_asset_module.h>

#include <engine/assets/key_factories/atmosphere.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    AtmosphereAssetModule::AtmosphereAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        AtmosphereTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    AtmosphereAsset AtmosphereAssetModule::create_atmosphere(
        const AtmosphereDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("atmosphere asset has empty name");
            return {};
        }

        // The typed meta the compiler prefers over a ParamBlock.
        AtmosphereCompileDesc compile_desc{};
        compile_desc.fog_color[0] = desc.fog_color[0];
        compile_desc.fog_color[1] = desc.fog_color[1];
        compile_desc.fog_color[2] = desc.fog_color[2];
        compile_desc.fog_density = desc.fog_density;
        compile_desc.fog_start_distance = desc.fog_start_distance;
        compile_desc.fog_height_falloff = desc.fog_height_falloff;
        compile_desc.fog_enabled = desc.fog_enabled;
        compile_desc.fog_saturation = desc.fog_saturation;

        const wz::asset::AssetKey key =
            make_atmosphere_key(
                desc.name,
                desc.fog_color,
                desc.fog_density,
                desc.fog_start_distance,
                desc.fog_height_falloff,
                desc.fog_enabled,
                desc.fog_saturation);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeAtmosphere;
        node.schema = kAtmosphereSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        // Atmosphere has no dependencies.
        system_.register_asset(std::move(node));

        return AtmosphereAsset{ .output = key };
    }

    AtmosphereHandle AtmosphereAssetModule::get_atmosphere(
        const AtmosphereAsset& asset) const
    {
        AtmosphereHandle out = find_atmosphere(asset);
        if (asset.valid() && !out.valid()) {
            logger_.error("atmosphere asset handle not found");
        }
        return out;
    }

    AtmosphereHandle AtmosphereAssetModule::find_atmosphere(
        const AtmosphereAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        AtmosphereHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        return out;
    }

    const AtmosphereData* AtmosphereAssetModule::get_atmosphere_data(
        AtmosphereHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }

        return table_.get(handle.handle);
    }
}
