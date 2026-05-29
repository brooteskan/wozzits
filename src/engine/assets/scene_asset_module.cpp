// src/engine/assets/scene_asset_module.cpp

#include <engine/assets/scene_asset_module.h>

#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/scene.h>

#include <algorithm>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        wz::asset::Hash scene_reference_bindings_hash(
            std::vector<SceneAssetReferenceBinding> refs,
            std::vector<SceneAssetReferenceBinding> terrain_refs = {},
            std::vector<SceneAssetReferenceBinding> mesh_refs = {},
            std::vector<SceneAssetReferenceBinding> scalar_field_refs = {})
        {
            refs.insert(
                refs.end(),
                terrain_refs.begin(),
                terrain_refs.end());
            refs.insert(
                refs.end(),
                mesh_refs.begin(),
                mesh_refs.end());
            refs.insert(
                refs.end(),
                scalar_field_refs.begin(),
                scalar_field_refs.end());

            if (refs.empty())
                return {};

            std::sort(refs.begin(), refs.end(),
                [](const auto& a, const auto& b) {
                    return a.uri < b.uri;
                });

            uint64_t lo = 0;
            uint64_t hi = 0;

            for (const auto& ref : refs) {
                const auto uri_hash = detail::hash_str(ref.uri);
                const auto key_hash = detail::key_to_dep_hash(ref.key);

                lo = detail::mix64(lo, uri_hash.lo);
                lo = detail::mix64(lo, key_hash.lo);
                hi = detail::mix64(hi, uri_hash.hi);
                hi = detail::mix64(hi, key_hash.hi);
            }

            return { lo, hi };
        }

        void append_unique_dependency(
            std::vector<wz::asset::AssetKey>& deps,
            wz::asset::AssetKey key)
        {
            if (key == wz::asset::AssetKey{})
                return;

            if (std::find(deps.begin(), deps.end(), key) == deps.end())
                deps.push_back(key);
        }

        void append_reference_dependencies(
            std::vector<wz::asset::AssetKey>& deps,
            const std::vector<SceneAssetReferenceBinding>& refs)
        {
            for (const auto& ref : refs)
                append_unique_dependency(deps, ref.key);
        }
    }

    SceneAssetModule::SceneAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        JSONAssetModule& json,
        SceneAssetTable& table)
        : system_(system)
        , logger_(logger)
        , files_(files)
        , json_(json)
        , table_(table)
    {
    }

    SceneAsset SceneAssetModule::create_scene_from_json(
        const SceneFromJSONDesc& desc)
    {
        const auto json_asset = json_.create_json({
            .name = desc.name + "_json",
            .path = desc.path,
        });

        if (!json_asset.valid()) {
            logger_.error("failed to create JSON document for scene: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey scene_key =
            make_scene_from_json_key(
                json_asset.output,
                scene_reference_bindings_hash(
                    desc.renderable_asset_references,
                    desc.terrain_asset_references,
                    desc.mesh_asset_references,
                    desc.scalar_field_asset_references));

        wz::asset::AssetNode node;
        node.key = scene_key;
        node.type = kAssetTypeScene;
        node.schema = kSceneFromJSONSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = SceneFromJSONCompileDesc{
            .renderable_asset_references =
                desc.renderable_asset_references,
            .terrain_asset_references = desc.terrain_asset_references,
            .mesh_asset_references = desc.mesh_asset_references,
            .scalar_field_asset_references =
                desc.scalar_field_asset_references,
        };

        std::vector<wz::asset::AssetKey> deps;
        append_unique_dependency(deps, json_asset.output);
        append_reference_dependencies(
            deps,
            desc.renderable_asset_references);
        append_reference_dependencies(
            deps,
            desc.terrain_asset_references);
        append_reference_dependencies(
            deps,
            desc.mesh_asset_references);
        append_reference_dependencies(
            deps,
            desc.scalar_field_asset_references);

        if (!system_.register_asset(std::move(node), std::move(deps))) {
            logger_.error("failed to register scene node: " + desc.name);
            return {};
        }

        return SceneAsset{ .output = scene_key };
    }

    SceneHandle SceneAssetModule::get_scene(const SceneAsset& asset) const
    {
        if (!asset.valid())
            return {};

        SceneHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("scene handle not found");

        return out;
    }

    const SceneAssetData* SceneAssetModule::get_scene_data(
        SceneHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

} // namespace wz::engine::assets
