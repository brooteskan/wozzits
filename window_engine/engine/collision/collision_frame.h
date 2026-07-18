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

    struct CollisionEntityEvent
    {
        wz::scene::RuntimeEntityId entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        wz::scene::RuntimeEntityId other =
            wz::scene::INVALID_RUNTIME_ENTITY;
        CollisionEventKind kind = CollisionEventKind::Enter;

        // Trigger state for the addressed entity. Exit events can reference
        // entities missing from the current world; those default to false.
        bool self_is_trigger = false;
    };

    struct ProximityWorldEntry
    {
        wz::scene::RuntimeEntityId entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        wz::math::Vec3 center{};
        float radius = 1.0f;
        uint32_t layer_mask = 1;
        uint32_t detects_with_mask = 0xffffffffu;
        bool enabled = true;
    };

    struct TerrainConstraintSurfaceEntry
    {
        wz::scene::RuntimeEntityId entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        wz::asset::AssetKey collision_asset{};
        wz::math::Mat4 world_from_local{ wz::math::Mat4::identity() };
        bool enabled = true;

        // Resolved frame-local pointer for movement-constraint terrain
        // sampling only. These entries do not participate in collision events.
        const wz::engine::assets::CollisionAssetData* resolved = nullptr;
    };

    enum class ProximityEventKind : uint8_t
    {
        Enter,
        Stay,
        Exit,
    };

    struct ProximityEvent
    {
        wz::scene::RuntimeEntityId a = wz::scene::INVALID_RUNTIME_ENTITY;
        wz::scene::RuntimeEntityId b = wz::scene::INVALID_RUNTIME_ENTITY;
        ProximityEventKind kind = ProximityEventKind::Enter;
    };

    struct ProximityEntityEvent
    {
        wz::scene::RuntimeEntityId entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        wz::scene::RuntimeEntityId other =
            wz::scene::INVALID_RUNTIME_ENTITY;
        ProximityEventKind kind = ProximityEventKind::Enter;
    };

    struct CollisionFrameStorage
    {
        // The observer this frame's queries are answered for.
        //
        // Almost every collision query is view-independent and ignores this. The
        // exception is a heightfield DRAWN as a geometry clipmap: its rings are
        // centred on the camera, so which ring covers a point -- and where that
        // ring's grid is snapped to -- move with it. Reconstructing the DRAWN
        // surface rather than the true field therefore needs the observer, and
        // this is the only thing every constraint caller already holds a
        // reference to.
        //
        // Written by the host each frame BEFORE build_collision_frame. Left at
        // the origin by every hand-built storage (unit tests, headless), which
        // is a well-defined observer rather than a missing one: with no clipmap
        // reconstruction configured nothing reads it, and with one configured
        // the origin puts the finest ring there.
        wz::math::Vec3 observer_world_position{};

        std::vector<CollisionWorldEntry> world;
        std::vector<TerrainConstraintSurfaceEntry>
            terrain_constraint_surfaces;
        std::vector<CollisionPair> broadphase_pairs;
        std::vector<CollisionPair> current_pairs;
        std::vector<CollisionPair> prev_pairs;
        std::vector<CollisionEvent> events;

        // Entity events preserve pair event order. For each pair event, the
        // event addressed to a is emitted before the event addressed to b.
        std::vector<CollisionEntityEvent> entity_events;

        // Entity events matched by the current scene event_listener snapshot.
        // Routing is a filter: channels do not dispatch callbacks or mutate
        // listeners.
        std::vector<CollisionEntityEvent> routed_entity_events;

        std::vector<ProximityWorldEntry> proximity_world;
        std::vector<CollisionPair> proximity_current_pairs;
        std::vector<CollisionPair> proximity_prev_pairs;
        std::vector<ProximityEvent> proximity_events;
        std::vector<ProximityEntityEvent> proximity_entity_events;
        std::vector<ProximityEntityEvent> routed_proximity_entity_events;
        uint32_t proximity_pairs_tested = 0;
        uint32_t proximity_pairs_rejected = 0;
        uint32_t proximity_pairs_matched = 0;

        uint32_t narrowphase_tests = 0;
        uint32_t terrain_cells_tested = 0;
        uint32_t terrain_cells_rejected = 0;
        uint32_t triangle_bounds_tested = 0;
        uint32_t triangle_bounds_rejected = 0;
        uint32_t early_out_hits = 0;
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

    void narrowphase_filter_pairs(
        std::span<const CollisionWorldEntry> world,
        std::span<const CollisionPair> candidate_pairs,
        CollisionFrameStorage& storage);

    void diff_collision_events(
        std::span<const CollisionPair> prev_pairs,
        std::span<const CollisionPair> current_pairs,
        std::vector<CollisionEvent>& out_events);

    void fanout_collision_entity_events(
        std::span<const CollisionWorldEntry> world,
        std::span<const CollisionEvent> pair_events,
        std::vector<CollisionEntityEvent>& out_entity_events);

    void route_collision_entity_events(
        std::span<const CollisionEntityEvent> entity_events,
        std::span<const wz::engine::assets::SceneComponentRecord<
            wz::engine::assets::EventListenerComponent>> listeners,
        std::vector<CollisionEntityEvent>& out_routed_events);
    // Recognized listener channels are exact tokens:
    // collision.enter, collision.stay, collision.exit, and collision.*.
    // collision.* is a collision-only wildcard token, not a glob pattern.

    bool proximity_masks_match(
        const ProximityWorldEntry& a,
        const ProximityWorldEntry& b) noexcept;

    void build_proximity_world(
        const wz::engine::assets::SceneInstance& scene,
        CollisionFrameStorage& storage);

    void broadphase_proximity_overlap(
        std::span<const ProximityWorldEntry> world,
        std::vector<CollisionPair>& out_pairs);

    void broadphase_proximity_overlap(
        std::span<const ProximityWorldEntry> world,
        std::vector<CollisionPair>& out_pairs,
        uint32_t& pairs_tested,
        uint32_t& pairs_rejected,
        uint32_t& pairs_matched);

    void diff_proximity_events(
        std::span<const CollisionPair> prev_pairs,
        std::span<const CollisionPair> current_pairs,
        std::vector<ProximityEvent>& out_events);

    void fanout_proximity_entity_events(
        std::span<const ProximityEvent> pair_events,
        std::vector<ProximityEntityEvent>& out_entity_events);

    void route_proximity_entity_events(
        std::span<const ProximityEntityEvent> entity_events,
        std::span<const wz::engine::assets::SceneComponentRecord<
            wz::engine::assets::EventListenerComponent>> listeners,
        std::vector<ProximityEntityEvent>& out_routed_events);
    // Recognized listener channels are exact tokens:
    // proximity.enter, proximity.stay, proximity.exit, and proximity.*.
    // proximity.* is a proximity-only wildcard token, not a glob pattern.

    void advance_proximity_frame(CollisionFrameStorage& storage);

    void advance_collision_frame(CollisionFrameStorage& storage);

    void build_collision_world(
        const wz::engine::assets::SceneInstance& scene,
        const wz::engine::assets::CollisionAssetModule& collisions,
        CollisionFrameStorage& storage);

    void build_collision_frame(
        const wz::engine::assets::SceneInstance& scene,
        const wz::engine::assets::CollisionAssetModule& collisions,
        CollisionFrameStorage& storage);

    void build_proximity_frame(
        const wz::engine::assets::SceneInstance& scene,
        CollisionFrameStorage& storage);
}
