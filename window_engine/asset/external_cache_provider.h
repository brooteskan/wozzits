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

        // Whether this provider serves (schema, type) at all when the entry is
        // present -- independent of whether any particular key's entry actually
        // exists. can_load conflates "this type is cache-backed" with "this key's
        // entry is on disk"; is_cacheable exposes only the former, so a resolve
        // can tell a genuinely-missing cacheable asset apart from a type that is
        // simply meant to compile at load (e.g. shaders). Default: not cacheable,
        // so a provider that doesn't override this is never treated as sealed.
        virtual bool is_cacheable(SchemaID /*schema*/, AssetType /*type*/) const
        {
            return false;
        }

        // Whether the cache is SEALED (a shipped bundle running from a read-only
        // baked cache with sources stripped, issue #334): a cacheable asset with
        // no entry is a fatal miss, not a recompile from an absent source.
        virtual bool sealed() const noexcept { return false; }
    };

} // namespace wz::asset
