#pragma once

// engine/assets/key_factories/vector_field.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/vector_field/vector_field.h>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::Hash vector_field_channels_hash(
        const std::vector<VectorFieldChannelDesc>& channels) noexcept
    {
        uint64_t lo = static_cast<uint64_t>(channels.size());
        uint64_t hi = 0;

        for (const auto& channel : channels) {
            const wz::asset::Hash name_hash =
                detail::hash_str(channel.name);
            lo = detail::mix64(lo, name_hash.lo);
            hi = detail::mix64(hi, name_hash.hi);
        }

        return { lo, hi };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_vector_field_key(
        const wz::asset::AssetKey& source_file_key,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint32_t components_per_channel,
        const std::vector<VectorFieldChannelDesc>& channels,
        uint8_t format_ordinal,
        uint8_t domain_kind_ordinal) noexcept
    {
        const uint64_t dims =
            (static_cast<uint64_t>(width) << 32)
            | static_cast<uint64_t>(height);

        const uint64_t depth_components =
            (static_cast<uint64_t>(depth) << 32)
            | static_cast<uint64_t>(components_per_channel);

        const uint64_t format_domain =
            (static_cast<uint64_t>(format_ordinal) << 8)
            | static_cast<uint64_t>(domain_kind_ordinal);

        const wz::asset::Hash channels_hash =
            vector_field_channels_hash(channels);

        const uint64_t lo =
            detail::mix64(
                detail::mix64(dims, depth_components),
                detail::mix64(format_domain, channels_hash.lo));

        const uint64_t hi = channels_hash.hi;

        return wz::asset::AssetKey{
            .content_hash = { lo, hi },
            .schema_hash = detail::hash_u64(kVectorFieldFromRawF32Schema.value),
            .compiler_hash = detail::hash_u64(kVectorFieldCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_file_key),
        };
    }

} // namespace wz::engine::assets
