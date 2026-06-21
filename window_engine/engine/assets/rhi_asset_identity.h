#pragma once

#include <asset/types.h>

#include <cstdint>
#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline uint64_t rhi_mix64(uint64_t seed, uint64_t value)
    {
        return seed
            ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
    }

    [[nodiscard]] inline uint64_t rhi_asset_identity(
        const wz::asset::AssetKey& key,
        std::string_view discriminator = {})
    {
        uint64_t h = 0xA55E7C001D00D5ull;
        h = rhi_mix64(h, key.content_hash.lo);
        h = rhi_mix64(h, key.content_hash.hi);
        h = rhi_mix64(h, key.schema_hash.lo);
        h = rhi_mix64(h, key.schema_hash.hi);
        h = rhi_mix64(h, key.compiler_hash.lo);
        h = rhi_mix64(h, key.compiler_hash.hi);
        h = rhi_mix64(h, key.deps_hash.lo);
        h = rhi_mix64(h, key.deps_hash.hi);
        for (const unsigned char c : discriminator) {
            h = rhi_mix64(h, static_cast<uint64_t>(c));
        }
        return h;
    }
}
