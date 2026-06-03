#pragma once

#include <asset/types.h>

#include <span>
#include <vector>

namespace wz::asset
{
    inline bool empty(const AssetKey& key)
    {
        return key == AssetKey{};
    }

    inline bool contains_key(
        std::span<const AssetKey> keys,
        const AssetKey& key)
    {
        for (const auto& existing : keys) {
            if (existing == key) {
                return true;
            }
        }
        return false;
    }

    inline bool append_unique_key(
        std::vector<AssetKey>& keys,
        const AssetKey& key)
    {
        if (contains_key(keys, key)) {
            return false;
        }

        keys.push_back(key);
        return true;
    }

    inline bool append_unique_non_empty_key(
        std::vector<AssetKey>& keys,
        const AssetKey& key)
    {
        if (empty(key)) {
            return false;
        }

        return append_unique_key(keys, key);
    }
}
