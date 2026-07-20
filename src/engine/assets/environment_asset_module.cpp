// src/engine/assets/environment_asset_module.cpp

#include <engine/assets/environment_asset_module.h>

#include <engine/assets/key_factories/environment.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    EnvironmentAssetModule::EnvironmentAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        EnvironmentTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    EnvironmentAsset EnvironmentAssetModule::create_environment(
        const EnvironmentDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("frame environment asset has empty name");
            return {};
        }

        const wz::asset::AssetKey key =
            make_environment_key(
                desc.name,
                desc.atmosphere,
                desc.ambient_lighting,
                desc.hdri_environment,
                desc.directional_light);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeFrameEnvironment;
        node.schema = kFrameEnvironmentSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};

        // Ports in fixed ROLE ORDER (atmosphere, ambient, HDRI, directional). An
        // empty key is an optional slot that creates no DAG edge
        // (AssetSystem::register_asset), so any subset is expressible, and the
        // order matches the deps_hash make_environment_key folded.
        system_.register_asset(
            std::move(node),
            { desc.atmosphere,
              desc.ambient_lighting,
              desc.hdri_environment,
              desc.directional_light });

        return EnvironmentAsset{ .output = key };
    }

    EnvironmentHandle EnvironmentAssetModule::get_environment(
        const EnvironmentAsset& asset) const
    {
        EnvironmentHandle out = find_environment(asset);
        if (asset.valid() && !out.valid()) {
            logger_.error("frame environment asset handle not found");
        }
        return out;
    }

    EnvironmentHandle EnvironmentAssetModule::find_environment(
        const EnvironmentAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        EnvironmentHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        return out;
    }

    const EnvironmentData* EnvironmentAssetModule::get_environment_data(
        EnvironmentHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }

        return table_.get(handle.handle);
    }
}
