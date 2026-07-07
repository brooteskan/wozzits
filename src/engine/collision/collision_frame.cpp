#include <engine/collision/collision_frame.h>

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <math/mat4.h>

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

        const CollisionWorldEntry* find_entry(
            std::span<const CollisionWorldEntry> world,
            wz::scene::RuntimeEntityId entity) noexcept
        {
            for (const auto& entry : world) {
                if (entry.entity == entity) {
                    return &entry;
                }
            }
            return nullptr;
        }

        bool entry_is_trigger(
            std::span<const CollisionWorldEntry> world,
            wz::scene::RuntimeEntityId entity) noexcept
        {
            const CollisionWorldEntry* entry = find_entry(world, entity);
            return entry ? entry->is_trigger : false;
        }

        WzBehaviorEventKind collision_behavior_event_kind(
            CollisionEventKind kind) noexcept
        {
            switch (kind) {
            case CollisionEventKind::Enter:
                return WZ_EVENT_COLLISION_ENTER;
            case CollisionEventKind::Stay:
                return WZ_EVENT_COLLISION_STAY;
            case CollisionEventKind::Exit:
                return WZ_EVENT_COLLISION_EXIT;
            }

            return WZ_EVENT_NONE;
        }

        WzBehaviorEventKind proximity_behavior_event_kind(
            ProximityEventKind kind) noexcept
        {
            switch (kind) {
            case ProximityEventKind::Enter:
                return WZ_EVENT_PROXIMITY_ENTER;
            case ProximityEventKind::Stay:
                return WZ_EVENT_PROXIMITY_STAY;
            case ProximityEventKind::Exit:
                return WZ_EVENT_PROXIMITY_EXIT;
            }

            return WZ_EVENT_NONE;
        }

        bool listener_matches_collision_event(
            const wz::engine::assets::EventListenerComponent& listener,
            const CollisionEntityEvent& event) noexcept
        {
            return wz::engine::assets::listener_accepts_event(
                listener,
                collision_behavior_event_kind(event.kind));
        }

        bool listener_matches_proximity_event(
            const wz::engine::assets::EventListenerComponent& listener,
            const ProximityEntityEvent& event) noexcept
        {
            return wz::engine::assets::listener_accepts_event(
                listener,
                proximity_behavior_event_kind(event.kind));
        }

        bool proximity_overlap(
            const ProximityWorldEntry& a,
            const ProximityWorldEntry& b) noexcept
        {
            const float dx = a.center.x - b.center.x;
            const float dy = a.center.y - b.center.y;
            const float dz = a.center.z - b.center.z;
            const float r = a.radius + b.radius;
            return dx * dx + dy * dy + dz * dz <= r * r;
        }

        bool inverse_affine_point(
            const wz::math::Mat4& m,
            const wz::math::Vec3& p,
            wz::math::Vec3& out) noexcept
        {
            const float a00 = m.m[0];
            const float a01 = m.m[4];
            const float a02 = m.m[8];
            const float a10 = m.m[1];
            const float a11 = m.m[5];
            const float a12 = m.m[9];
            const float a20 = m.m[2];
            const float a21 = m.m[6];
            const float a22 = m.m[10];

            const float det =
                a00 * (a11 * a22 - a12 * a21)
                - a01 * (a10 * a22 - a12 * a20)
                + a02 * (a10 * a21 - a11 * a20);
            if (std::abs(det) <= 1e-8f) {
                return false;
            }

            const float inv_det = 1.0f / det;
            const float x = p.x - m.m[12];
            const float y = p.y - m.m[13];
            const float z = p.z - m.m[14];

            out.x =
                ((a11 * a22 - a12 * a21) * x
                    + (a02 * a21 - a01 * a22) * y
                    + (a01 * a12 - a02 * a11) * z)
                * inv_det;
            out.y =
                ((a12 * a20 - a10 * a22) * x
                    + (a00 * a22 - a02 * a20) * y
                    + (a02 * a10 - a00 * a12) * z)
                * inv_det;
            out.z =
                ((a10 * a21 - a11 * a20) * x
                    + (a01 * a20 - a00 * a21) * y
                    + (a00 * a11 - a01 * a10) * z)
                * inv_det;
            return true;
        }

        bool world_aabb_to_local(
            const wz::scene::AABB& world,
            const wz::math::Mat4& local_to_world,
            wz::scene::AABB& out) noexcept
        {
            const wz::math::Vec3 corners[8] = {
                { world.min.x, world.min.y, world.min.z },
                { world.max.x, world.min.y, world.min.z },
                { world.min.x, world.max.y, world.min.z },
                { world.max.x, world.max.y, world.min.z },
                { world.min.x, world.min.y, world.max.z },
                { world.max.x, world.min.y, world.max.z },
                { world.min.x, world.max.y, world.max.z },
                { world.max.x, world.max.y, world.max.z },
            };

            out = wz::scene::AABB{
                .min = { FLT_MAX, FLT_MAX, FLT_MAX },
                .max = { -FLT_MAX, -FLT_MAX, -FLT_MAX },
            };

            for (const auto& corner : corners) {
                wz::math::Vec3 local{};
                if (!inverse_affine_point(local_to_world, corner, local)) {
                    return false;
                }
                out.min.x = (std::min)(out.min.x, local.x);
                out.min.y = (std::min)(out.min.y, local.y);
                out.min.z = (std::min)(out.min.z, local.z);
                out.max.x = (std::max)(out.max.x, local.x);
                out.max.y = (std::max)(out.max.y, local.y);
                out.max.z = (std::max)(out.max.z, local.z);
            }

            return true;
        }

        bool triangle_bounds_overlap(
            const wz::engine::assets::CollisionTriangleBounds& tri,
            const wz::scene::AABB& bounds) noexcept
        {
            return tri.min[0] <= bounds.max.x && tri.max[0] >= bounds.min.x
                && tri.min[1] <= bounds.max.y && tri.max[1] >= bounds.min.y
                && tri.min[2] <= bounds.max.z && tri.max[2] >= bounds.min.z;
        }

        bool query_surface_grid_overlap(
            const wz::engine::assets::CollisionAssetData& data,
            const wz::scene::AABB& local_bounds,
            uint32_t& terrain_cells_tested,
            uint32_t& terrain_cells_rejected,
            uint32_t& triangle_bounds_tested,
            uint32_t& triangle_bounds_rejected,
            uint32_t& early_out_hits)
        {
            const auto& grid = data.surface_grid;
            if (grid.cells_x == 0
                || grid.cells_z == 0
                || grid.cell_offsets.size()
                    != static_cast<size_t>(grid.cells_x) * grid.cells_z + 1u)
            {
                return false;
            }

            const float grid_max_x =
                grid.origin_x + grid.cell_size_x * grid.cells_x;
            const float grid_max_z =
                grid.origin_z + grid.cell_size_z * grid.cells_z;
            if (local_bounds.max.x < grid.origin_x
                || local_bounds.min.x > grid_max_x
                || local_bounds.max.z < grid.origin_z
                || local_bounds.min.z > grid_max_z)
            {
                return false;
            }

            auto cell_index = [](float value, float origin, float size, uint32_t count) {
                const float normalized = (value - origin) / size;
                const int raw = static_cast<int>(std::floor(normalized));
                return static_cast<uint32_t>(
                    (std::clamp)(raw, 0, static_cast<int>(count) - 1));
            };

            const uint32_t min_x = cell_index(
                local_bounds.min.x,
                grid.origin_x,
                grid.cell_size_x,
                grid.cells_x);
            const uint32_t max_x = cell_index(
                local_bounds.max.x,
                grid.origin_x,
                grid.cell_size_x,
                grid.cells_x);
            const uint32_t min_z = cell_index(
                local_bounds.min.z,
                grid.origin_z,
                grid.cell_size_z,
                grid.cells_z);
            const uint32_t max_z = cell_index(
                local_bounds.max.z,
                grid.origin_z,
                grid.cell_size_z,
                grid.cells_z);

            for (uint32_t z = min_z; z <= max_z; ++z) {
                for (uint32_t x = min_x; x <= max_x; ++x) {
                    const size_t cell =
                        static_cast<size_t>(z) * grid.cells_x + x;
                    ++terrain_cells_tested;
                    if (cell < grid.cell_bounds.size()
                        && !triangle_bounds_overlap(
                            grid.cell_bounds[cell],
                            local_bounds))
                    {
                        ++terrain_cells_rejected;
                        continue;
                    }

                    const uint32_t begin = grid.cell_offsets[cell];
                    const uint32_t end = grid.cell_offsets[cell + 1u];
                    for (uint32_t i = begin; i < end; ++i) {
                        const uint32_t tri =
                            grid.cell_triangle_indices[i];
                        if (tri >= data.triangle_bounds.size()) {
                            continue;
                        }

                        ++triangle_bounds_tested;
                        if (triangle_bounds_overlap(
                                data.triangle_bounds[tri],
                                local_bounds))
                        {
                            ++early_out_hits;
                            return true;
                        }
                        ++triangle_bounds_rejected;
                    }
                }
            }

            return false;
        }

        bool bounds_vs_triangle_surface_overlap(
            const CollisionWorldEntry& bounds_entry,
            const CollisionWorldEntry& surface_entry,
            uint32_t& terrain_cells_tested,
            uint32_t& terrain_cells_rejected,
            uint32_t& triangle_bounds_tested,
            uint32_t& triangle_bounds_rejected,
            uint32_t& early_out_hits)
        {
            if (!surface_entry.resolved) {
                return false;
            }

            const auto& data = *surface_entry.resolved;
            wz::scene::AABB local_bounds{};
            if (!world_aabb_to_local(
                    bounds_entry.world_bounds,
                    surface_entry.world_from_local,
                    local_bounds))
            {
                return true;
            }

            if (query_surface_grid_overlap(
                    data,
                    local_bounds,
                    terrain_cells_tested,
                    terrain_cells_rejected,
                    triangle_bounds_tested,
                    triangle_bounds_rejected,
                    early_out_hits))
            {
                return true;
            }

            if (data.surface_grid.cells_x != 0) {
                return false;
            }

            for (const auto& tri : data.triangle_bounds) {
                ++triangle_bounds_tested;
                if (triangle_bounds_overlap(tri, local_bounds)) {
                    ++early_out_hits;
                    return true;
                }
                ++triangle_bounds_rejected;
            }

            return false;
        }

        bool collision_narrowphase_overlap(
            const CollisionWorldEntry& a,
            const CollisionWorldEntry& b,
            uint32_t& terrain_cells_tested,
            uint32_t& terrain_cells_rejected,
            uint32_t& triangle_bounds_tested,
            uint32_t& triangle_bounds_rejected,
            uint32_t& early_out_hits)
        {
            using Shape = wz::engine::assets::CollisionShapeKind;

            if (!a.resolved || !b.resolved) {
                // Missing shape data has already been screened by world build
                // where possible; keep unresolved pairs conservative here.
                return true;
            }

            const Shape a_shape = a.resolved->shape_kind;
            const Shape b_shape = b.resolved->shape_kind;

            if (a_shape == Shape::Bounds
                && b_shape == Shape::TerrainMeshSurface)
            {
                return bounds_vs_triangle_surface_overlap(
                    a,
                    b,
                    terrain_cells_tested,
                    terrain_cells_rejected,
                    triangle_bounds_tested,
                    triangle_bounds_rejected,
                    early_out_hits);
            }
            if (a_shape == Shape::TerrainMeshSurface
                && b_shape == Shape::Bounds)
            {
                return bounds_vs_triangle_surface_overlap(
                    b,
                    a,
                    terrain_cells_tested,
                    terrain_cells_rejected,
                    triangle_bounds_tested,
                    triangle_bounds_rejected,
                    early_out_hits);
            }

            return true;
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

    void fanout_collision_entity_events(
        std::span<const CollisionWorldEntry> world,
        std::span<const CollisionEvent> pair_events,
        std::vector<CollisionEntityEvent>& out_entity_events)
    {
        out_entity_events.clear();
        out_entity_events.reserve(pair_events.size() * 2u);

        for (const CollisionEvent& event : pair_events) {
            out_entity_events.push_back({
                .entity = event.a,
                .other = event.b,
                .kind = event.kind,
                .self_is_trigger = entry_is_trigger(world, event.a),
            });
            out_entity_events.push_back({
                .entity = event.b,
                .other = event.a,
                .kind = event.kind,
                .self_is_trigger = entry_is_trigger(world, event.b),
            });
        }
    }

    void route_collision_entity_events(
        std::span<const CollisionEntityEvent> entity_events,
        std::span<const wz::engine::assets::SceneComponentRecord<
            wz::engine::assets::EventListenerComponent>> listeners,
        std::vector<CollisionEntityEvent>& out_routed_events)
    {
        out_routed_events.clear();
        out_routed_events.reserve(entity_events.size());

        for (const CollisionEntityEvent& event : entity_events) {
            for (const auto& listener : listeners) {
                if (listener.node == event.entity
                    && listener_matches_collision_event(
                        listener.component,
                        event))
                {
                    out_routed_events.push_back(event);
                    break;
                }
            }
        }
    }

    bool proximity_masks_match(
        const ProximityWorldEntry& a,
        const ProximityWorldEntry& b) noexcept
    {
        return (a.layer_mask & b.detects_with_mask) != 0u
            && (b.layer_mask & a.detects_with_mask) != 0u;
    }

    void build_proximity_world(
        const wz::engine::assets::SceneInstance& scene,
        CollisionFrameStorage& storage)
    {
        storage.proximity_world.clear();

        const uint32_t node_count = static_cast<uint32_t>(
            wz::core::graph::node_count(scene.storage.polytree));

        for (const auto& record : scene.proximities) {
            const auto& component = record.component;
            if (!component.enabled
                || !scene.entity_is_active(record.node)  // #252 "live?" gate
                || component.radius <= 0.0f
                || !std::isfinite(component.radius)
                || record.node == wz::scene::INVALID_RUNTIME_ENTITY
                || record.node >= node_count)
            {
                continue;
            }

            const auto& node = wz::core::graph::node_data(
                scene.storage.polytree,
                record.node);
            storage.proximity_world.push_back(ProximityWorldEntry{
                .entity = record.node,
                .center = {
                    node.world.m[12],
                    node.world.m[13],
                    node.world.m[14],
                },
                .radius = component.radius,
                .layer_mask = component.layer_mask,
                .detects_with_mask = component.detects_with_mask,
                .enabled = component.enabled,
            });
        }
    }

    void broadphase_proximity_overlap(
        std::span<const ProximityWorldEntry> world,
        std::vector<CollisionPair>& out_pairs)
    {
        out_pairs.clear();
        for (size_t i = 0; i < world.size(); ++i) {
            const auto& a = world[i];
            if (!a.enabled) {
                continue;
            }
            for (size_t j = i + 1u; j < world.size(); ++j) {
                const auto& b = world[j];
                if (!b.enabled
                    || !proximity_masks_match(a, b)
                    || !proximity_overlap(a, b))
                {
                    continue;
                }
                const CollisionPair pair = make_collision_pair(
                    a.entity,
                    b.entity);
                if (collision_pair_valid(pair)) {
                    out_pairs.push_back(pair);
                }
            }
        }
        sort_unique_collision_pairs(out_pairs);
    }

    void broadphase_proximity_overlap(
        std::span<const ProximityWorldEntry> world,
        std::vector<CollisionPair>& out_pairs,
        uint32_t& pairs_tested,
        uint32_t& pairs_rejected,
        uint32_t& pairs_matched)
    {
        out_pairs.clear();
        pairs_tested = 0;
        pairs_rejected = 0;
        pairs_matched = 0;

        for (size_t i = 0; i < world.size(); ++i) {
            const auto& a = world[i];
            if (!a.enabled) {
                continue;
            }
            for (size_t j = i + 1u; j < world.size(); ++j) {
                const auto& b = world[j];
                if (!b.enabled) {
                    continue;
                }

                ++pairs_tested;
                if (!proximity_masks_match(a, b)
                    || !proximity_overlap(a, b))
                {
                    ++pairs_rejected;
                    continue;
                }

                const CollisionPair pair = make_collision_pair(
                    a.entity,
                    b.entity);
                if (collision_pair_valid(pair)) {
                    out_pairs.push_back(pair);
                    ++pairs_matched;
                }
            }
        }
        sort_unique_collision_pairs(out_pairs);
    }

    void diff_proximity_events(
        std::span<const CollisionPair> prev_pairs,
        std::span<const CollisionPair> current_pairs,
        std::vector<ProximityEvent>& out_events)
    {
        out_events.clear();
        out_events.reserve(prev_pairs.size() + current_pairs.size());

        size_t prev_i = 0;
        size_t current_i = 0;
        while (prev_i < prev_pairs.size() || current_i < current_pairs.size()) {
            if (prev_i >= prev_pairs.size()) {
                const auto& current = current_pairs[current_i++];
                out_events.push_back({
                    .a = current.a,
                    .b = current.b,
                    .kind = ProximityEventKind::Enter,
                });
                continue;
            }
            if (current_i >= current_pairs.size()) {
                const auto& prev = prev_pairs[prev_i++];
                out_events.push_back({
                    .a = prev.a,
                    .b = prev.b,
                    .kind = ProximityEventKind::Exit,
                });
                continue;
            }

            const auto& prev = prev_pairs[prev_i];
            const auto& current = current_pairs[current_i];
            if (prev == current) {
                out_events.push_back({
                    .a = current.a,
                    .b = current.b,
                    .kind = ProximityEventKind::Stay,
                });
                ++prev_i;
                ++current_i;
            }
            else if (prev < current) {
                out_events.push_back({
                    .a = prev.a,
                    .b = prev.b,
                    .kind = ProximityEventKind::Exit,
                });
                ++prev_i;
            }
            else {
                out_events.push_back({
                    .a = current.a,
                    .b = current.b,
                    .kind = ProximityEventKind::Enter,
                });
                ++current_i;
            }
        }
    }

    void fanout_proximity_entity_events(
        std::span<const ProximityEvent> pair_events,
        std::vector<ProximityEntityEvent>& out_entity_events)
    {
        out_entity_events.clear();
        out_entity_events.reserve(pair_events.size() * 2u);

        for (const ProximityEvent& event : pair_events) {
            out_entity_events.push_back({
                .entity = event.a,
                .other = event.b,
                .kind = event.kind,
            });
            out_entity_events.push_back({
                .entity = event.b,
                .other = event.a,
                .kind = event.kind,
            });
        }
    }

    void route_proximity_entity_events(
        std::span<const ProximityEntityEvent> entity_events,
        std::span<const wz::engine::assets::SceneComponentRecord<
            wz::engine::assets::EventListenerComponent>> listeners,
        std::vector<ProximityEntityEvent>& out_routed_events)
    {
        out_routed_events.clear();
        out_routed_events.reserve(entity_events.size());

        for (const ProximityEntityEvent& event : entity_events) {
            for (const auto& listener : listeners) {
                if (listener.node == event.entity
                    && listener_matches_proximity_event(
                        listener.component,
                        event))
                {
                    out_routed_events.push_back(event);
                    break;
                }
            }
        }
    }

    void advance_proximity_frame(CollisionFrameStorage& storage)
    {
        sort_unique_collision_pairs(storage.proximity_prev_pairs);
        sort_unique_collision_pairs(storage.proximity_current_pairs);
        diff_proximity_events(
            storage.proximity_prev_pairs,
            storage.proximity_current_pairs,
            storage.proximity_events);
        fanout_proximity_entity_events(
            storage.proximity_events,
            storage.proximity_entity_events);
        storage.routed_proximity_entity_events.clear();
        storage.proximity_prev_pairs = storage.proximity_current_pairs;
    }

    void advance_collision_frame(CollisionFrameStorage& storage)
    {
        sort_unique_collision_pairs(storage.prev_pairs);
        sort_unique_collision_pairs(storage.current_pairs);
        diff_collision_events(
            storage.prev_pairs,
            storage.current_pairs,
            storage.events);
        fanout_collision_entity_events(
            storage.world,
            storage.events,
            storage.entity_events);
        storage.routed_entity_events.clear();
        storage.prev_pairs = storage.current_pairs;
    }

    void narrowphase_filter_pairs(
        std::span<const CollisionWorldEntry> world,
        std::span<const CollisionPair> candidate_pairs,
        CollisionFrameStorage& storage)
    {
        storage.current_pairs.clear();
        storage.narrowphase_tests = 0;
        storage.terrain_cells_tested = 0;
        storage.terrain_cells_rejected = 0;
        storage.triangle_bounds_tested = 0;
        storage.triangle_bounds_rejected = 0;
        storage.early_out_hits = 0;

        for (const CollisionPair& pair : candidate_pairs) {
            const CollisionWorldEntry* a = find_entry(world, pair.a);
            const CollisionWorldEntry* b = find_entry(world, pair.b);
            if (!a || !b) {
                continue;
            }

            ++storage.narrowphase_tests;
            if (collision_narrowphase_overlap(
                    *a,
                    *b,
                    storage.terrain_cells_tested,
                    storage.terrain_cells_rejected,
                    storage.triangle_bounds_tested,
                    storage.triangle_bounds_rejected,
                    storage.early_out_hits))
            {
                storage.current_pairs.push_back(pair);
            }
        }

        sort_unique_collision_pairs(storage.current_pairs);
    }

    void build_collision_world(
        const wz::engine::assets::SceneInstance& scene,
        const wz::engine::assets::CollisionAssetModule& collisions,
        CollisionFrameStorage& storage)
    {
        storage.world.clear();
        storage.terrain_constraint_surfaces.clear();

        const uint32_t node_count = static_cast<uint32_t>(
            wz::core::graph::node_count(scene.storage.polytree));

        for (const auto& record : scene.collisions) {
            const auto& component = record.component;
            if (!component.enabled
                || !scene.entity_is_active(record.node)  // #252 "live?" gate
                || record.node == wz::scene::INVALID_RUNTIME_ENTITY
                || record.node >= node_count)
            {
                continue;
            }

            const auto handle = collisions.find_collision(
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

            // Placement-driven collision bakes world-frame bounds/mapping at
            // compile time (issue #218), so the carrying node's transform must
            // NOT be composed on top (issue #224); use identity instead.
            const wz::math::Mat4 collision_from_local =
                data->placement_driven
                    ? wz::math::Mat4::identity()
                    : node.world;

            storage.world.push_back(CollisionWorldEntry{
                .entity = record.node,
                .collision_asset = component.collision_asset,
                .world_from_local = collision_from_local,
                .world_bounds = wz::scene::transform_aabb(
                    local_bounds,
                    collision_from_local),
                .layer_mask = component.layer_mask,
                .collides_with_mask = component.collides_with_mask,
                .is_trigger = component.is_trigger,
                .enabled = component.enabled,
                .resolved = data,
            });
        }

        for (const auto& record : scene.terrains) {
            const auto& component = record.component;
            if (!component.constrain_movement
                || !scene.entity_is_active(record.node)  // #252 "live?" gate
                || component.constraint_surface_asset == wz::asset::AssetKey{}
                || record.node == wz::scene::INVALID_RUNTIME_ENTITY
                || record.node >= node_count)
            {
                continue;
            }

            const auto handle = collisions.find_collision(
                wz::engine::assets::CollisionAsset{
                    .output = component.constraint_surface_asset,
                });
            const auto* data = collisions.get_collision_data(handle);
            if (!data) {
                continue;
            }

            const auto& node = wz::core::graph::node_data(
                scene.storage.polytree,
                record.node);
            storage.terrain_constraint_surfaces.push_back(
                TerrainConstraintSurfaceEntry{
                    .entity = record.node,
                    .collision_asset =
                        component.constraint_surface_asset,
                    // Placement-driven data is already world-frame (issue
                    // #224); skip the carrying node transform.
                    .world_from_local = data->placement_driven
                        ? wz::math::Mat4::identity()
                        : node.world,
                    .enabled = true,
                    .resolved = data,
                });
        }

        // Collision components authored as movement-constraint surfaces (e.g.
        // a scalar-field heightfield collision with no TerrainAsset) also feed
        // the terrain-constraint pipeline.
        for (const auto& record : scene.collisions) {
            const auto& component = record.component;
            if (!component.constrain_movement
                || !scene.entity_is_active(record.node)  // #252 "live?" gate
                || component.collision_asset == wz::asset::AssetKey{}
                || record.node == wz::scene::INVALID_RUNTIME_ENTITY
                || record.node >= node_count)
            {
                continue;
            }

            const auto handle = collisions.find_collision(
                wz::engine::assets::CollisionAsset{
                    .output = component.collision_asset,
                });
            const auto* data = collisions.get_collision_data(handle);
            if (!data || !data->supports_height_query) {
                continue;
            }

            const auto& node = wz::core::graph::node_data(
                scene.storage.polytree,
                record.node);
            storage.terrain_constraint_surfaces.push_back(
                TerrainConstraintSurfaceEntry{
                    .entity = record.node,
                    .collision_asset = component.collision_asset,
                    // Placement-driven data is already world-frame (issue
                    // #224); skip the carrying node transform.
                    .world_from_local = data->placement_driven
                        ? wz::math::Mat4::identity()
                        : node.world,
                    .enabled = true,
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
        broadphase_aabb_overlap(storage.world, storage.broadphase_pairs);
        narrowphase_filter_pairs(
            storage.world,
            storage.broadphase_pairs,
            storage);
        advance_collision_frame(storage);
        route_collision_entity_events(
            storage.entity_events,
            scene.event_listeners,
            storage.routed_entity_events);
    }

    void build_proximity_frame(
        const wz::engine::assets::SceneInstance& scene,
        CollisionFrameStorage& storage)
    {
        build_proximity_world(scene, storage);
        broadphase_proximity_overlap(
            storage.proximity_world,
            storage.proximity_current_pairs,
            storage.proximity_pairs_tested,
            storage.proximity_pairs_rejected,
            storage.proximity_pairs_matched);
        advance_proximity_frame(storage);
        route_proximity_entity_events(
            storage.proximity_entity_events,
            scene.event_listeners,
            storage.routed_proximity_entity_events);
    }
}
