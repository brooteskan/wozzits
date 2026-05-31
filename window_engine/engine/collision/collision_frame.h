#pragma once

// engine/collision/collision_frame.h

#include <asset/types.h>
#include <engine/assets/collision_asset_module.h>
#include <engine/assets/collision/collision.h>
#include <engine/assets/scene/scene_instance.h>

#include <math/mat4.h>
#include <scene/geometry.h>
#include <scene/scene_ecs.h>

#include <cstdint>
#include <span>
#include <vector>

namespace wz::engine::collision
{
    struct CollisionPair
    {
        wz::scene::RuntimeEntityId a = wz::scene::INVALID_RUNTIME_ENTITY;
        wz::scene::RuntimeEntityId b = wz::scene::INVALID_RUNTIME_ENTITY;

        constexpr auto operator<=>(const CollisionPair&) const = default;
    };

    struct CollisionWorldEntry
    {
        wz::scene::RuntimeEntityId entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        wz::asset::AssetKey collision_asset{};
        wz::math::Mat4 world_from_local{ wz::math::Mat4::identity() };
        wz::scene::AABB world_bounds{};
        uint32_t layer_mask = 1;
        uint32_t collides_with_mask = 0xffffffffu;
        bool is_trigger = false;
        bool enabled = true;

        // Frame-local cache for future narrowphase work. Pair history stays
        // pointer-free and only stores stable runtime entity ids.
        const wz::engine::assets::CollisionAssetData* resolved = nullptr;
    };

    enum class CollisionEventKind : uint8_t
    {
        Enter,
        Stay,
        Exit,
    };

    struct CollisionEvent
    {
        wz::scene::RuntimeEntityId a = wz::scene::INVALID_RUNTIME_ENTITY;
        wz::scene::RuntimeEntityId b = wz::scene::INVALID_RUNTIME_ENTITY;
        CollisionEventKind kind = CollisionEventKind::Enter;
    };

    struct CollisionFrameStorage
    {
        std::vector<CollisionWorldEntry> world;
        std::vector<CollisionPair> current_pairs;
        std::vector<CollisionPair> prev_pairs;
        std::vector<CollisionEvent> events;
    };

    CollisionPair make_collision_pair(
        wz::scene::RuntimeEntityId a,
        wz::scene::RuntimeEntityId b) noexcept;

    bool collision_pair_valid(const CollisionPair& pair) noexcept;

    bool aabb_overlap(
        const wz::scene::AABB& a,
        const wz::scene::AABB& b) noexcept;

    bool collision_masks_match(
        const CollisionWorldEntry& a,
        const CollisionWorldEntry& b) noexcept;

    void sort_unique_collision_pairs(std::vector<CollisionPair>& pairs);

    void broadphase_aabb_overlap(
        std::span<const CollisionWorldEntry> world,
        std::vector<CollisionPair>& out_pairs);

    void diff_collision_events(
        std::span<const CollisionPair> prev_pairs,
        std::span<const CollisionPair> current_pairs,
        std::vector<CollisionEvent>& out_events);

    void advance_collision_frame(CollisionFrameStorage& storage);

    void build_collision_world(
        const wz::engine::assets::SceneInstance& scene,
        const wz::engine::assets::CollisionAssetModule& collisions,
        CollisionFrameStorage& storage);

    void build_collision_frame(
        const wz::engine::assets::SceneInstance& scene,
        const wz::engine::assets::CollisionAssetModule& collisions,
        CollisionFrameStorage& storage);
}
