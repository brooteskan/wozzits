#pragma once
// engine/assets/key.h

#include <span>
#include <string_view>
#include <asset/types.h>
#include <engine/assets/schema_registry.h>

namespace wz::asset
{

    inline AssetKey make_hlsl_file_key(std::span<const uint8_t> bytes, std::string_view path) {
        return AssetKey{
            .content_hash = FNV(bytes),          // changes when file changes
            .schema_hash = hash_of(kHLSLFileSchema),
            .compiler_hash = {},                     // file nodes have no compiler
            .deps_hash = hash_of(path),          // path is a stable disambiguator
        };
    }
}