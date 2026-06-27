// src/engine/assets/collision_asset_module.cpp

#include <engine/assets/collision_asset_module.h>

#include <engine/assets/key_factories/collision.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    CollisionAssetModule::CollisionAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        CollisionAssetTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    CollisionAsset CollisionAssetModule::create_from_mesh(
        const CollisionFromMeshDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("mesh collision asset has empty name");
            return {};
        }
        if (!desc.mesh.valid()) {
            logger_.error("mesh collision asset has invalid source: "
                + desc.name);
            return {};
        }

        CollisionFromMeshCompileDesc compile_desc{};
        compile_desc.mesh = desc.mesh.output;
        compile_desc.build_method = desc.build_method;
        compile_desc.occupancy = desc.occupancy;

        const wz::asset::AssetKey key =
            make_collision_from_mesh_key(
                desc.name,
                desc.mesh.output,
                desc.build_method,
                desc.occupancy);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeCollisionAsset;
        node.schema = kCollisionFromMeshSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(std::move(node), { desc.mesh.output })) {
            return CollisionAsset{ .output = key };
        }

        return CollisionAsset{ .output = key };
    }

    CollisionAsset CollisionAssetModule::create_from_terrain(
        const CollisionFromTerrainDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("terrain collision asset has empty name");
            return {};
        }
        if (!desc.terrain.valid()) {
            logger_.error("terrain collision asset has invalid source: "
                + desc.name);
            return {};
        }

        CollisionFromTerrainCompileDesc compile_desc{};
        compile_desc.terrain = desc.terrain.output;
        compile_desc.build_method = desc.build_method;
        compile_desc.occupancy = desc.occupancy;
        compile_desc.projection_resolution_x =
            desc.projection_resolution_x;
        compile_desc.projection_resolution_y =
            desc.projection_resolution_y;

        const wz::asset::AssetKey key =
            make_collision_from_terrain_key(
                desc.name,
                desc.terrain.output,
                desc.build_method,
                desc.occupancy,
                desc.projection_resolution_x,
                desc.projection_resolution_y);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeCollisionAsset;
        node.schema = kCollisionFromTerrainSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(std::move(node), { desc.terrain.output })) {
            return CollisionAsset{ .output = key };
        }

        return CollisionAsset{ .output = key };
    }

    CollisionAsset CollisionAssetModule::create_from_height_field(
        const CollisionFromHeightFieldDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("height field collision asset has empty name");
            return {};
        }
        if (!desc.height_field.valid()) {
            logger_.error(
                "height field collision asset has invalid source: "
                + desc.name);
            return {};
        }

        CollisionFromHeightFieldCompileDesc compile_desc{};
        compile_desc.height_field = desc.height_field.output;
        compile_desc.origin[0] = desc.origin[0];
        compile_desc.origin[1] = desc.origin[1];
        compile_desc.size[0] = desc.size[0];
        compile_desc.size[1] = desc.size[1];
        compile_desc.vertical_scale = desc.vertical_scale;
        compile_desc.base_height = desc.base_height;
        compile_desc.occupancy = desc.occupancy;
        compile_desc.projection_resolution_x = desc.projection_resolution_x;
        compile_desc.projection_resolution_y = desc.projection_resolution_y;

        const wz::asset::AssetKey key =
            make_collision_from_height_field_key(
                desc.name,
                desc.height_field.output,
                desc.origin,
                desc.size,
                desc.vertical_scale,
                desc.base_height,
                desc.occupancy,
                desc.projection_resolution_x,
                desc.projection_resolution_y);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeCollisionAsset;
        node.schema = kCollisionFromHeightFieldSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(
                std::move(node),
                { desc.height_field.output }))
        {
            return CollisionAsset{ .output = key };
        }

        return CollisionAsset{ .output = key };
    }

    CollisionHandle CollisionAssetModule::get_collision(
        const CollisionAsset& asset) const
    {
        CollisionHandle out = find_collision(asset);
        if (asset.valid() && !out.valid()) {
            logger_.error("collision asset handle not found");
        }
        return out;
    }

    CollisionHandle CollisionAssetModule::find_collision(
        const CollisionAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        CollisionHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        return out;
    }

    const CollisionAssetData* CollisionAssetModule::get_collision_data(
        CollisionHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }

        return table_.get(handle.handle);
    }
}
