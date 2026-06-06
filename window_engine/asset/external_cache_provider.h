#pragma once

#include "types.h"

#include <optional>

namespace wz::asset {

    class ExternalCacheProvider {
    public:
        virtual ~ExternalCacheProvider() = default;

        virtual bool can_load(
            SchemaID schema,
            AssetType type,
            const AssetKey& key) const = 0;

        virtual std::optional<ResourceHandle> load(
            SchemaID schema,
            AssetType type,
            const AssetKey& key) = 0;
    };

} // namespace wz::asset
