// src/engine/assets/terrain_asset_module.cpp

#include <engine/assets/terrain_asset_module.h>

#include <engine/assets/key_factories/terrain.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    TerrainAssetModule::TerrainAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        TerrainAssetTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    TerrainAsset TerrainAssetModule::create_from_height_field(
        const TerrainFromHeightFieldDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("heightfield terrain has empty name");
            return {};
        }
        if (!desc.height_field.valid()) {
            logger_.error("heightfield terrain has invalid source: "
                + desc.name);
            return {};
        }
        if (desc.size[0] <= 0.0f || desc.size[1] <= 0.0f) {
            logger_.error("heightfield terrain has non-positive size: "
                + desc.name);
            return {};
        }

        TerrainFromHeightFieldCompileDesc compile_desc{};
        compile_desc.height_field = desc.height_field.output;
        compile_desc.origin[0] = desc.origin[0];
        compile_desc.origin[1] = desc.origin[1];
        compile_desc.size[0] = desc.size[0];
        compile_desc.size[1] = desc.size[1];
        compile_desc.vertical_scale = desc.vertical_scale;
        compile_desc.base_height = desc.base_height;
        compile_desc.normal_field = desc.normal_field;
        compile_desc.material_mask_set = desc.material_mask_set;
        compile_desc.render_mode = desc.render_mode;
        compile_desc.collision_mode = desc.collision_mode;

        const wz::asset::AssetKey key =
            make_terrain_from_height_field_key(
                desc.name,
                desc.height_field.output,
                desc.size[0],
                desc.size[1],
                desc.vertical_scale,
                desc.base_height,
                static_cast<uint8_t>(desc.render_mode),
                static_cast<uint8_t>(desc.collision_mode));

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeTerrain;
        node.schema = kTerrainFromHeightFieldSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(
                std::move(node),
                { desc.height_field.output }))
        {
            logger_.error("failed to register heightfield terrain: "
                + desc.name);
            return {};
        }

        return TerrainAsset{ .output = key };
    }

    TerrainAsset TerrainAssetModule::create_from_mesh(
        const TerrainFromMeshDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("mesh terrain has empty name");
            return {};
        }
        if (!desc.mesh.valid()) {
            logger_.error("mesh terrain has invalid source: " + desc.name);
            return {};
        }

        TerrainFromMeshCompileDesc compile_desc{};
        compile_desc.mesh = desc.mesh.output;
        compile_desc.render_mode = desc.render_mode;
        compile_desc.collision_mode = desc.collision_mode;

        const wz::asset::AssetKey key =
            make_terrain_from_mesh_key(
                desc.name,
                desc.mesh.output,
                static_cast<uint8_t>(desc.render_mode),
                static_cast<uint8_t>(desc.collision_mode));

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeTerrain;
        node.schema = kTerrainFromMeshSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(std::move(node), { desc.mesh.output })) {
            logger_.error("failed to register mesh terrain: " + desc.name);
            return {};
        }

        return TerrainAsset{ .output = key };
    }

    TerrainHandle TerrainAssetModule::get_terrain(
        const TerrainAsset& asset) const
    {
        if (!asset.valid())
            return {};

        TerrainHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        if (!out.valid()) {
            logger_.error("terrain handle not found");
        }
        return out;
    }

    const TerrainAssetData* TerrainAssetModule::get_terrain_data(
        TerrainHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}
