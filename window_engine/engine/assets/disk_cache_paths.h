#pragma once

#include <asset/types.h>
#include <engine/assets/asset_cache_settings.h>
#include <file/filesystem.h>

#include <iomanip>
#include <sstream>
#include <string>

namespace wz::engine::assets::internal
{
    inline uint64_t mix_disk_cache_key_word(uint64_t state, uint64_t word)
    {
        state ^= word + 0x9e3779b97f4a7c15ull + (state << 6) + (state >> 2);
        state ^= state >> 30;
        state *= 0xbf58476d1ce4e5b9ull;
        state ^= state >> 27;
        state *= 0x94d049bb133111ebull;
        return state ^ (state >> 31);
    }

    inline std::string short_disk_cache_key_hex(
        const wz::asset::AssetKey& key,
        uint64_t lo_seed,
        uint64_t hi_seed)
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

        uint64_t lo = lo_seed;
        uint64_t hi = hi_seed;
        for (uint64_t word : words) {
            lo = mix_disk_cache_key_word(lo, word);
            hi = mix_disk_cache_key_word(hi, word ^ lo);
        }

        std::ostringstream out;
        out << std::hex << std::setfill('0')
            << std::setw(16) << lo
            << std::setw(16) << hi;
        return out.str();
    }

    inline wz::fs::Path disk_cache_asset_directory(
        const EngineAssetCacheSettings& cache,
        const char* subdirectory)
    {
        return wz::fs::join(
            wz::fs::join(cache.root, "assets"),
            subdirectory);
    }

    inline wz::fs::Path disk_cache_asset_path(
        const EngineAssetCacheSettings& cache,
        const char* subdirectory,
        const wz::asset::AssetKey& key,
        uint64_t lo_seed,
        uint64_t hi_seed)
    {
        return wz::fs::join(
            disk_cache_asset_directory(cache, subdirectory),
            short_disk_cache_key_hex(key, lo_seed, hi_seed) + ".bin");
    }

    inline bool disk_cache_asset_exists(
        const EngineAssetCacheSettings& cache,
        const char* subdirectory,
        const wz::asset::AssetKey& key,
        uint64_t lo_seed,
        uint64_t hi_seed)
    {
        return cache.enabled
            && !cache.root.empty()
            && wz::fs::exists(disk_cache_asset_path(
                cache,
                subdirectory,
                key,
                lo_seed,
                hi_seed));
    }
}
