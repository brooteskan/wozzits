#include <engine/assets/light_asset_module.h>

#include <engine/assets/key_factories/light.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    LightAssetModule::LightAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        DirectLightTable& direct_light_table,
        AmbientLightingTable& ambient_lighting_table)
        : system_(system)
        , logger_(logger)
        , direct_light_table_(direct_light_table)
        , ambient_lighting_table_(ambient_lighting_table)
    {
    }

    DirectLightAsset LightAssetModule::create_direct_light(
        const DirectLightDesc& desc)
    {
        DirectLightCompileDesc compile_desc{};
        compile_desc.kind = desc.kind;
        compile_desc.color[0] = desc.color[0];
        compile_desc.color[1] = desc.color[1];
        compile_desc.color[2] = desc.color[2];
        compile_desc.intensity = desc.intensity;
        compile_desc.range = desc.range;
        compile_desc.inner_cone_radians = desc.inner_cone_radians;
        compile_desc.outer_cone_radians = desc.outer_cone_radians;

        if (!compile_desc.valid()) {
            logger_.error("direct light asset desc is invalid: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_direct_light_key(desc.name, compile_desc);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeDirectLight;
        node.schema = kDirectLightSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(std::move(node), {})) {
            return DirectLightAsset{ .output = key };
        }

        return DirectLightAsset{ .output = key };
    }

    AmbientLightingAsset LightAssetModule::create_ambient_lighting(
        const AmbientLightingDesc& desc)
    {
        AmbientLightingCompileDesc compile_desc{};
        compile_desc.mode = desc.mode;
        compile_desc.color[0] = desc.color[0];
        compile_desc.color[1] = desc.color[1];
        compile_desc.color[2] = desc.color[2];
        compile_desc.intensity = desc.intensity;
        compile_desc.intensity_field = desc.intensity_field;
        compile_desc.color_field = desc.color_field;
        compile_desc.domain_mapping = desc.domain_mapping;

        if (!compile_desc.valid()) {
            logger_.error("ambient lighting asset desc is invalid: "
                + desc.name);
            return {};
        }

        std::vector<wz::asset::AssetKey> deps;
        if (!(compile_desc.intensity_field == wz::asset::AssetKey{})) {
            deps.push_back(compile_desc.intensity_field);
        }
        if (!(compile_desc.color_field == wz::asset::AssetKey{})) {
            deps.push_back(compile_desc.color_field);
        }

        const wz::asset::AssetKey key =
            make_ambient_lighting_key(desc.name, compile_desc);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeAmbientLighting;
        node.schema = kAmbientLightingSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(std::move(node), deps)) {
            return AmbientLightingAsset{ .output = key };
        }

        return AmbientLightingAsset{ .output = key };
    }

    DirectLightHandle LightAssetModule::get_direct_light(
        const DirectLightAsset& asset) const
    {
        DirectLightHandle out{};
        if (!asset.valid()) {
            return out;
        }
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        if (!out.valid()) {
            logger_.error("direct light handle not found");
        }
        return out;
    }

    AmbientLightingHandle LightAssetModule::get_ambient_lighting(
        const AmbientLightingAsset& asset) const
    {
        AmbientLightingHandle out{};
        if (!asset.valid()) {
            return out;
        }
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        if (!out.valid()) {
            logger_.error("ambient lighting handle not found");
        }
        return out;
    }

    const DirectLightData* LightAssetModule::get_direct_light_data(
        DirectLightHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        return direct_light_table_.get(handle.handle);
    }

    const AmbientLightingData* LightAssetModule::get_ambient_lighting_data(
        AmbientLightingHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        return ambient_lighting_table_.get(handle.handle);
    }

} // namespace wz::engine::assets
