#pragma once

// engine/assets/key_factories/clipmap_lattice_schedule.h
//
// Key factory for ClipmapLatticeSchedule nodes.
//
// A schedule is fully determined by its authored dials (world_extent, horizon,
// triangle_budget) and the ONE height field it descends from: those four inputs
// are exactly what the compiler feeds resolve_clipmap_lattice. content_hash
// folds the dials, deps_hash folds the field.
//
// Why the field goes in deps_hash rather than content_hash:
//   The compiler reads only ONE number out of the field — N, its texel count per
//   side — but N is a property of the field's identity, so folding the field key
//   makes a resolution change (a different heightmap, a re-imported one at
//   another size) re-key the schedule and every consumer of it, without the
//   schedule having to mirror N as an authored parameter that could go stale.
//
// Why include name?
//   Two schedules with identical dials over the same field would otherwise
//   produce the same key and the second registration would silently fail.
//   Including name lets callers author distinct schedules that share parameters
//   (same rationale as the placement / procedural scalar field key factories).

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline uint64_t clipmap_lattice_schedule_hash_float(
        float value) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    [[nodiscard]] inline wz::asset::AssetKey make_clipmap_lattice_schedule_key(
        std::string_view name,
        const wz::asset::AssetKey& scalar_field_key,
        float world_extent,
        float horizon,
        uint32_t triangle_budget) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, clipmap_lattice_schedule_hash_float(world_extent));
        h = detail::mix64(h, clipmap_lattice_schedule_hash_float(horizon));
        h = detail::mix64(h, triangle_budget);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kClipmapLatticeScheduleSchema.value),
            .compiler_hash =
                detail::hash_u64(kClipmapLatticeScheduleCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(scalar_field_key),
        };
    }
}
