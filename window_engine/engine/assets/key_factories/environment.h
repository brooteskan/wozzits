#pragma once

// engine/assets/key_factories/environment.h
//
// Key factory for FrameEnvironment nodes.
//
// A FrameEnvironment is a pure aggregator: its identity is its name plus WHICH
// pieces it references. content_hash folds the name; deps_hash folds the four
// referenced piece keys.
//
// Dep ordering: FrameEnvironment's ports are semantically UNORDERED — a bundle
// keyed by ROLE, located by asset type, not position. So the deps are folded in
// a FIXED ROLE ORDER (atmosphere, ambient lighting, HDRI, directional light),
// each role's key folded whether or not it is connected (an unconnected role
// folds its zero key). That makes the key depend only on WHICH assets fill WHICH
// roles, never on the order the editor happened to wire the edges, and keeps it
// stable as roles are added or removed. This is the "sort/fix the order and
// document it" discipline combine_dep_hashes calls for in engine_asset_key_core.h.
// The module registers its dep vector in this SAME role order (empties are
// optional slots), so the folded key and the DAG edges agree.
//
// Why include name?
//   Two environments referencing the same pieces would otherwise produce the
//   same key and the second registration would silently fail. Including name lets
//   callers author distinct environments that share references (same rationale as
//   the placement / atmosphere key factories).

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_environment_key(
        std::string_view name,
        const wz::asset::AssetKey& atmosphere_key,
        const wz::asset::AssetKey& ambient_lighting_key,
        const wz::asset::AssetKey& hdri_environment_key,
        const wz::asset::AssetKey& directional_light_key) noexcept
    {
        // Fixed role order; an unconnected role folds its zero key, so the result
        // depends on which asset fills which role, not on edge-wiring order.
        wz::asset::Hash deps = detail::key_to_dep_hash(atmosphere_key);
        deps = detail::combine_dep_hashes(
            deps, detail::key_to_dep_hash(ambient_lighting_key));
        deps = detail::combine_dep_hashes(
            deps, detail::key_to_dep_hash(hdri_environment_key));
        deps = detail::combine_dep_hashes(
            deps, detail::key_to_dep_hash(directional_light_key));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(detail::fnv1a_64(name)),
            .schema_hash = detail::hash_u64(kFrameEnvironmentSchema.value),
            .compiler_hash =
                detail::hash_u64(kFrameEnvironmentCompilerVersion),
            .deps_hash = deps,
        };
    }
}
