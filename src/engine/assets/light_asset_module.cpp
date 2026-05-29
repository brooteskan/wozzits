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
        FileCarrierAssetModule& files,
        DirectLightTable& direct_light_table,
        AmbientLightingTable& ambient_lighting_table,
        HDRIEnvironmentTable& hdri_environment_table)
        : system_(system)
        , logger_(logger)
        , files_(files)
        , direct_light_table_(direct_light_table)
        , ambient_lighting_table_(ambient_lighting_table)
        , hdri_environment_table_(hdri_environment_table)
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

    HDRIEnvironmentAsset LightAssetModule::create_hdri_environment(
        const HDRIEnvironmentDesc& desc)
    {
        const wz::asset::AssetKey file_key = files_.register_file_node(
            desc.path,
            kImportedSourceFileSchema,
            kAssetTypeImportedSourceFile);

        if (file_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register HDRI environment source file: "
                + desc.path);
            return {};
        }

        HDRIEnvironmentCompileDesc compile_desc{};
        compile_desc.source_file = file_key;
        compile_desc.format = desc.format;
        compile_desc.exposure = desc.exposure;
        compile_desc.rotation_y_radians = desc.rotation_y_radians;
        compile_desc.lighting_intensity = desc.lighting_intensity;
        compile_desc.reflection_intensity = desc.reflection_intensity;
        compile_desc.background_intensity = desc.background_intensity;
        compile_desc.dominant_light_direction[0] =
            desc.dominant_light_direction[0];
        compile_desc.dominant_light_direction[1] =
            desc.dominant_light_direction[1];
        compile_desc.dominant_light_direction[2] =
            desc.dominant_light_direction[2];
        compile_desc.dominant_light_color[0] = desc.dominant_light_color[0];
        compile_desc.dominant_light_color[1] = desc.dominant_light_color[1];
        compile_desc.dominant_light_color[2] = desc.dominant_light_color[2];
        compile_desc.dominant_light_intensity =
            desc.dominant_light_intensity;
        compile_desc.dominant_light_confidence =
            desc.dominant_light_confidence;

        if (!compile_desc.valid()) {
            logger_.error("HDRI environment asset desc is invalid: "
                + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_hdri_environment_key(desc.name, compile_desc);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeEnvironmentMap;
        node.schema = kHDRIEnvironmentSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(std::move(node), { file_key })) {
            return HDRIEnvironmentAsset{ .output = key };
        }

        return HDRIEnvironmentAsset{ .output = key };
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

    HDRIEnvironmentHandle LightAssetModule::get_hdri_environment(
        const HDRIEnvironmentAsset& asset) const
    {
        HDRIEnvironmentHandle out{};
        if (!asset.valid()) {
            return out;
        }
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        if (!out.valid()) {
            logger_.error("HDRI environment handle not found");
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

    const HDRIEnvironmentData* LightAssetModule::get_hdri_environment_data(
        HDRIEnvironmentHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        return hdri_environment_table_.get(handle.handle);
    }

} // namespace wz::engine::assets
