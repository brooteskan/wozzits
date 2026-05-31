#include <engine/collision/collision_frame.h>

#include <algorithm>

namespace wz::engine::collision
{
    namespace
    {
        bool aabb_valid(const wz::scene::AABB& a) noexcept
        {
            return a.min.x <= a.max.x
                && a.min.y <= a.max.y
                && a.min.z <= a.max.z;
        }
    }

    CollisionPair make_collision_pair(
        wz::scene::RuntimeEntityId a,
        wz::scene::RuntimeEntityId b) noexcept
    {
        return (a <= b)
            ? CollisionPair{ .a = a, .b = b }
            : CollisionPair{ .a = b, .b = a };
    }

    bool collision_pair_valid(const CollisionPair& pair) noexcept
    {
        return pair.a != wz::scene::INVALID_RUNTIME_ENTITY
            && pair.b != wz::scene::INVALID_RUNTIME_ENTITY
            && pair.a != pair.b;
    }

    bool aabb_overlap(
        const wz::scene::AABB& a,
        const wz::scene::AABB& b) noexcept
    {
        if (!aabb_valid(a) || !aabb_valid(b)) {
            return false;
        }

        return a.min.x <= b.max.x && a.max.x >= b.min.x
            && a.min.y <= b.max.y && a.max.y >= b.min.y
            && a.min.z <= b.max.z && a.max.z >= b.min.z;
    }

    bool collision_masks_match(
        const CollisionWorldEntry& a,
        const CollisionWorldEntry& b) noexcept
    {
        return (a.collides_with_mask & b.layer_mask) != 0
            && (b.collides_with_mask & a.layer_mask) != 0;
    }

    void sort_unique_collision_pairs(std::vector<CollisionPair>& pairs)
    {
        pairs.erase(
            std::remove_if(
                pairs.begin(),
                pairs.end(),
                [](const CollisionPair& pair) {
                    return !collision_pair_valid(pair);
                }),
            pairs.end());

        for (auto& pair : pairs) {
            pair = make_collision_pair(pair.a, pair.b);
        }

        std::sort(pairs.begin(), pairs.end());
        pairs.erase(
            std::unique(pairs.begin(), pairs.end()),
            pairs.end());
    }

    void broadphase_aabb_overlap(
        std::span<const CollisionWorldEntry> world,
        std::vector<CollisionPair>& out_pairs)
    {
        out_pairs.clear();

        for (std::size_t i = 0; i < world.size(); ++i) {
            const auto& a = world[i];
            if (!a.enabled) {
                continue;
            }

            for (std::size_t j = i + 1; j < world.size(); ++j) {
                const auto& b = world[j];
                if (!b.enabled
                    || !collision_masks_match(a, b)
                    || !aabb_overlap(a.world_bounds, b.world_bounds))
                {
                    continue;
                }

                out_pairs.push_back(make_collision_pair(a.entity, b.entity));
            }
        }

        sort_unique_collision_pairs(out_pairs);
    }

    void diff_collision_events(
        std::span<const CollisionPair> prev_pairs,
        std::span<const CollisionPair> current_pairs,
        std::vector<CollisionEvent>& out_events)
    {
        out_events.clear();

        std::size_t prev_i = 0;
        std::size_t current_i = 0;

        while (prev_i < prev_pairs.size()
            || current_i < current_pairs.size())
        {
            if (current_i >= current_pairs.size()) {
                const CollisionPair pair = prev_pairs[prev_i++];
                out_events.push_back({
                    .a = pair.a,
                    .b = pair.b,
                    .kind = CollisionEventKind::Exit,
                });
                continue;
            }

            if (prev_i >= prev_pairs.size()) {
                const CollisionPair pair = current_pairs[current_i++];
                out_events.push_back({
                    .a = pair.a,
                    .b = pair.b,
                    .kind = CollisionEventKind::Enter,
                });
                continue;
            }

            const CollisionPair prev = prev_pairs[prev_i];
            const CollisionPair current = current_pairs[current_i];

            if (prev == current) {
                out_events.push_back({
                    .a = current.a,
                    .b = current.b,
                    .kind = CollisionEventKind::Stay,
                });
                ++prev_i;
                ++current_i;
            }
            else if (prev < current) {
                out_events.push_back({
                    .a = prev.a,
                    .b = prev.b,
                    .kind = CollisionEventKind::Exit,
                });
                ++prev_i;
            }
            else {
                out_events.push_back({
                    .a = current.a,
                    .b = current.b,
                    .kind = CollisionEventKind::Enter,
                });
                ++current_i;
            }
        }
    }

    void advance_collision_frame(CollisionFrameStorage& storage)
    {
        sort_unique_collision_pairs(storage.prev_pairs);
        sort_unique_collision_pairs(storage.current_pairs);
        diff_collision_events(
            storage.prev_pairs,
            storage.current_pairs,
            storage.events);
        storage.prev_pairs = storage.current_pairs;
    }

    void build_collision_world(
        const wz::engine::assets::SceneInstance& scene,
        const wz::engine::assets::CollisionAssetModule& collisions,
        CollisionFrameStorage& storage)
    {
        storage.world.clear();

        const uint32_t node_count = static_cast<uint32_t>(
            wz::core::graph::node_count(scene.storage.polytree));

        for (const auto& record : scene.collisions) {
            const auto& component = record.component;
            if (!component.enabled
                || record.node == wz::scene::INVALID_RUNTIME_ENTITY
                || record.node >= node_count)
            {
                continue;
            }

            const auto handle = collisions.get_collision(
                wz::engine::assets::CollisionAsset{
                    .output = component.collision_asset,
                });
            const auto* data = collisions.get_collision_data(handle);
            if (!data || !data->supports_bounds_query) {
                continue;
            }

            const auto& node = wz::core::graph::node_data(
                scene.storage.polytree,
                record.node);

            const wz::scene::AABB local_bounds{
                .min = {
                    data->bounds_min[0],
                    data->bounds_min[1],
                    data->bounds_min[2],
                },
                .max = {
                    data->bounds_max[0],
                    data->bounds_max[1],
                    data->bounds_max[2],
                },
            };

            storage.world.push_back(CollisionWorldEntry{
                .entity = record.node,
                .collision_asset = component.collision_asset,
                .world_from_local = node.world,
                .world_bounds = wz::scene::transform_aabb(
                    local_bounds,
                    node.world),
                .layer_mask = component.layer_mask,
                .collides_with_mask = component.collides_with_mask,
                .is_trigger = component.is_trigger,
                .enabled = component.enabled,
                .resolved = data,
            });
        }
    }

    void build_collision_frame(
        const wz::engine::assets::SceneInstance& scene,
        const wz::engine::assets::CollisionAssetModule& collisions,
        CollisionFrameStorage& storage)
    {
        build_collision_world(scene, collisions, storage);
        broadphase_aabb_overlap(storage.world, storage.current_pairs);
        advance_collision_frame(storage);
    }
}
