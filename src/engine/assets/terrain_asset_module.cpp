// src/engine/assets/terrain_asset_module.cpp

#include <engine/assets/terrain_asset_module.h>

#include <engine/assets/key_factories/terrain.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <file/filesystem.h>

#include <iomanip>
#include <sstream>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        uint64_t mix_cache_key_word(uint64_t state, uint64_t word)
        {
            state ^= word + 0x9e3779b97f4a7c15ull + (state << 6) + (state >> 2);
            state ^= state >> 30;
            state *= 0xbf58476d1ce4e5b9ull;
            state ^= state >> 27;
            state *= 0x94d049bb133111ebull;
            return state ^ (state >> 31);
        }

        std::string short_terrain_cache_key_hex(const wz::asset::AssetKey& key)
        {
            const uint64_t words[]{
                key.content_hash.lo,
                key.content_hash.hi,
                key.schema_hash.lo,
                key.schema_hash.hi,
                key.compiler_hash.lo,
                key.compiler_hash.hi,
                key.deps_hash.lo,
                key.deps_hash.hi,
            };

            uint64_t lo = 0xa4093822299f31d0ull;
            uint64_t hi = 0x082efa98ec4e6c89ull;
            for (uint64_t word : words) {
                lo = mix_cache_key_word(lo, word);
                hi = mix_cache_key_word(hi, word ^ lo);
            }

            std::ostringstream out;
            out << std::hex << std::setfill('0')
                << std::setw(16) << lo
                << std::setw(16) << hi;
            return out.str();
        }

        wz::fs::Path mesh_terrain_cache_path(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key)
        {
            return wz::fs::join(
                wz::fs::join(
                    wz::fs::join(cache.root, "assets"),
                    "mesh_terrain"),
                short_terrain_cache_key_hex(key) + ".bin");
        }

        bool cached_mesh_terrain_exists(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key)
        {
            return cache.enabled
                && !cache.root.empty()
                && wz::fs::exists(mesh_terrain_cache_path(cache, key));
        }
    }

    TerrainAssetModule::TerrainAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        TerrainAssetTable& table,
        const EngineAssetCacheSettings& cache_settings)
        : system_(system)
        , logger_(logger)
        , table_(table)
        , cache_settings_(cache_settings)
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
            return TerrainAsset{ .output = key };

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
        compile_desc.height_policy = desc.height_policy;
        compile_desc.min_surface_normal_y = desc.min_surface_normal_y;
        compile_desc.include_backfaces = desc.include_backfaces;
        compile_desc.preferred_normal_source = desc.preferred_normal_source;
        compile_desc.preferred_uv_source = desc.preferred_uv_source;
        compile_desc.render_mode = desc.render_mode;
        compile_desc.collision_mode = desc.collision_mode;
        compile_desc.visual_chunk_count =
            desc.visual_chunk_count == 0u ? 4096u : desc.visual_chunk_count;

        const wz::asset::AssetKey key =
            make_terrain_from_mesh_key(
                desc.name,
                desc.mesh.output,
                static_cast<uint8_t>(desc.height_policy),
                desc.min_surface_normal_y,
                desc.include_backfaces,
                static_cast<uint8_t>(desc.preferred_normal_source),
                static_cast<uint8_t>(desc.preferred_uv_source),
                static_cast<uint8_t>(desc.render_mode),
                static_cast<uint8_t>(desc.collision_mode),
                compile_desc.visual_chunk_count);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeTerrain;
        node.schema = kTerrainFromMeshSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        std::vector<wz::asset::AssetKey> deps;
        if (!cached_mesh_terrain_exists(cache_settings_, key)) {
            deps.push_back(desc.mesh.output);
        }

        if (!system_.register_asset(std::move(node), std::move(deps)))
            return TerrainAsset{ .output = key };

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
