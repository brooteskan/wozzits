#include <engine/behavior/behavior_command_apply.h>

#include <engine/collision/collision_surface_sampling.h>

#include <algorithm>
#include <cmath>
#include <math/mat4.h>
#include <math/quaternion.h>
#include <math/vec3.h>
#include <scene/scene_graph.h>

namespace wz::engine::behavior
{
    namespace
    {
        bool entity_valid(
            const wz::engine::assets::SceneInstance& scene,
            wz::scene::RuntimeEntityId entity) noexcept
        {
            return entity != wz::scene::INVALID_RUNTIME_ENTITY
                && entity < wz::core::graph::node_count(
                    scene.storage.polytree);
        }

        float column_length(
            const wz::math::Mat4& matrix,
            uint32_t column) noexcept
        {
            const uint32_t i = column * 4u;
            return std::sqrt(
                matrix.m[i + 0u] * matrix.m[i + 0u]
                + matrix.m[i + 1u] * matrix.m[i + 1u]
                + matrix.m[i + 2u] * matrix.m[i + 2u]);
        }

        void set_column_scale(
            wz::math::Mat4& matrix,
            uint32_t column,
            float scale) noexcept
        {
            const uint32_t i = column * 4u;
            const float length = column_length(matrix, column);
            if (length > 0.0f) {
                const float factor = scale / length;
                matrix.m[i + 0u] *= factor;
                matrix.m[i + 1u] *= factor;
                matrix.m[i + 2u] *= factor;
                return;
            }

            matrix.m[i + 0u] = column == 0u ? scale : 0.0f;
            matrix.m[i + 1u] = column == 1u ? scale : 0.0f;
            matrix.m[i + 2u] = column == 2u ? scale : 0.0f;
        }

        void set_local_scale(
            wz::math::Mat4& matrix,
            float x,
            float y,
            float z) noexcept
        {
            set_column_scale(matrix, 0u, x);
            set_column_scale(matrix, 1u, y);
            set_column_scale(matrix, 2u, z);
        }

        void add_local_scale(
            wz::math::Mat4& matrix,
            float x,
            float y,
            float z) noexcept
        {
            set_local_scale(
                matrix,
                column_length(matrix, 0u) + x,
                column_length(matrix, 1u) + y,
                column_length(matrix, 2u) + z);
        }

        wz::math::Vec3 normalized_column(
            const wz::math::Mat4& matrix,
            uint32_t column) noexcept
        {
            const uint32_t i = column * 4u;
            const float length = column_length(matrix, column);
            if (length <= 0.0f) {
                return {
                    .x = column == 0u ? 1.0f : 0.0f,
                    .y = column == 1u ? 1.0f : 0.0f,
                    .z = column == 2u ? 1.0f : 0.0f,
                };
            }

            return {
                .x = matrix.m[i + 0u] / length,
                .y = matrix.m[i + 1u] / length,
                .z = matrix.m[i + 2u] / length,
            };
        }

        wz::math::Vec3 local_motion_to_world(
            const wz::math::Mat4& world,
            float x,
            float y,
            float z) noexcept
        {
            const wz::math::Vec3 axis_x = normalized_column(world, 0u);
            const wz::math::Vec3 axis_y = normalized_column(world, 1u);
            const wz::math::Vec3 axis_z = normalized_column(world, 2u);
            return {
                .x = axis_x.x * x + axis_y.x * y + axis_z.x * z,
                .y = axis_x.y * x + axis_y.y * y + axis_z.y * z,
                .z = axis_x.z * x + axis_y.z * y + axis_z.z * z,
            };
        }

        wz::math::Quaternion inverse_unit_quaternion(
            const wz::math::Quaternion& q) noexcept
        {
            const wz::math::Quaternion n = wz::math::normalize(q);
            return { -n.x, -n.y, -n.z, n.w };
        }

        bool normalize_vec3_checked(
            wz::math::Vec3& v,
            float epsilon = 1e-6f) noexcept
        {
            const float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
            if (len_sq <= epsilon * epsilon || !std::isfinite(len_sq)) {
                return false;
            }
            v = wz::math::normalize(v);
            return true;
        }

        wz::math::Quaternion nlerp_shortest(
            wz::math::Quaternion from,
            wz::math::Quaternion to,
            float t) noexcept
        {
            from = wz::math::normalize(from);
            to = wz::math::normalize(to);
            if (wz::math::dot(from, to) < 0.0f) {
                to.x = -to.x;
                to.y = -to.y;
                to.z = -to.z;
                to.w = -to.w;
            }

            return wz::math::normalize(wz::math::Quaternion{
                .x = from.x + (to.x - from.x) * t,
                .y = from.y + (to.y - from.y) * t,
                .z = from.z + (to.z - from.z) * t,
                .w = from.w + (to.w - from.w) * t,
            });
        }

        bool terrain_aligned_world_rotation(
            const wz::math::Quaternion& current_world_rotation,
            const wz::math::Vec3& surface_normal,
            float strength,
            float max_step_radians,
            wz::math::Quaternion& out_rotation) noexcept
        {
            wz::math::Vec3 up = surface_normal;
            if (!normalize_vec3_checked(up)) {
                return false;
            }
            if (up.y < 0.0f) {
                up.x = -up.x;
                up.y = -up.y;
                up.z = -up.z;
            }

            const wz::math::Quaternion current =
                wz::math::normalize(current_world_rotation);
            wz::math::Vec3 forward = wz::math::quat_axis_z(current);
            const float forward_dot_up = wz::math::dot(forward, up);
            forward = {
                .x = forward.x - up.x * forward_dot_up,
                .y = forward.y - up.y * forward_dot_up,
                .z = forward.z - up.z * forward_dot_up,
            };
            if (!normalize_vec3_checked(forward)) {
                wz::math::Vec3 right = wz::math::quat_axis_x(current);
                const float right_dot_up = wz::math::dot(right, up);
                right = {
                    .x = right.x - up.x * right_dot_up,
                    .y = right.y - up.y * right_dot_up,
                    .z = right.z - up.z * right_dot_up,
                };
                if (!normalize_vec3_checked(right)) {
                    right = std::abs(up.y) < 0.95f
                        ? wz::math::Vec3{ .x = 0.0f, .y = 1.0f, .z = 0.0f }
                        : wz::math::Vec3{ .x = 1.0f, .y = 0.0f, .z = 0.0f };
                    right = wz::math::cross(right, up);
                    if (!normalize_vec3_checked(right)) {
                        return false;
                    }
                }
                forward = wz::math::cross(right, up);
                if (!normalize_vec3_checked(forward)) {
                    return false;
                }
            }

            wz::math::Vec3 right = wz::math::cross(up, forward);
            if (!normalize_vec3_checked(right)) {
                return false;
            }
            forward = wz::math::cross(right, up);
            if (!normalize_vec3_checked(forward)) {
                return false;
            }

            wz::math::Mat4 basis = wz::math::Mat4::identity();
            basis.m[0] = right.x;
            basis.m[1] = right.y;
            basis.m[2] = right.z;
            basis.m[4] = up.x;
            basis.m[5] = up.y;
            basis.m[6] = up.z;
            basis.m[8] = forward.x;
            basis.m[9] = forward.y;
            basis.m[10] = forward.z;

            const wz::math::Quaternion desired =
                wz::math::from_rotation_matrix(basis);

            // Blend factor toward the desired alignment. A positive
            // max_step_radians rate-limits the swing: cap the step to that angle
            // this frame (frame-rate independent, since the caller passes
            // rate * delta_seconds). Otherwise fall back to the instantaneous
            // strength blend.
            float t;
            if (max_step_radians > 0.0f) {
                const float d = (std::clamp)(
                    std::abs(wz::math::dot(current, desired)), 0.0f, 1.0f);
                const float angle = 2.0f * std::acos(d);  // radians, [0, pi]
                t = angle > 1e-5f
                    ? (std::min)(1.0f, max_step_radians / angle)
                    : 1.0f;
            }
            else {
                t = (std::clamp)(strength, 0.0f, 1.0f);
            }
            out_rotation = t >= 1.0f
                ? desired
                : nlerp_shortest(current, desired, t);
            return true;
        }

        bool integrate_angular_velocity(
            wz::scene::SceneGraph& graph,
            wz::scene::TransformNode& node,
            wz::scene::RuntimeEntityId entity,
            const wz::engine::assets::MotionComponent& motion,
            float delta_seconds) noexcept
        {
            const wz::math::Vec3 angular_velocity{
                .x = motion.angular_velocity[0],
                .y = motion.angular_velocity[1],
                .z = motion.angular_velocity[2],
            };
            const float magnitude = wz::math::length(angular_velocity);
            if (magnitude <= 0.0f || !std::isfinite(magnitude)) {
                return false;
            }

            const float delta_angle = magnitude * delta_seconds;
            if (delta_angle == 0.0f || !std::isfinite(delta_angle)) {
                return false;
            }

            wz::math::Transform local_trs{};
            if (!wz::math::decompose_trs(node.local, local_trs)) {
                return false;
            }

            const wz::math::Vec3 axis = angular_velocity / magnitude;
            const wz::math::Quaternion delta =
                wz::math::from_axis_angle(axis, delta_angle);

            if (motion.space == wz::engine::assets::SceneMotionSpace::Local) {
                local_trs.rotation = wz::math::normalize(
                    wz::math::mul(local_trs.rotation, delta));
                node.local = wz::math::transform(local_trs);
                return true;
            }

            const auto parent = wz::core::graph::parent(graph, entity);
            if (parent == wz::core::graph::INVALID_NODE) {
                local_trs.rotation = wz::math::normalize(
                    wz::math::mul(delta, local_trs.rotation));
                node.local = wz::math::transform(local_trs);
                return true;
            }

            wz::math::Transform parent_world_trs{};
            if (!wz::math::decompose_trs(
                    wz::core::graph::node_data(graph, parent).world,
                    parent_world_trs))
            {
                return false;
            }

            wz::math::Transform world_trs{};
            if (!wz::math::decompose_trs(node.world, world_trs)) {
                return false;
            }

            const wz::math::Quaternion new_world_rotation =
                wz::math::normalize(wz::math::mul(delta, world_trs.rotation));
            local_trs.rotation = wz::math::normalize(wz::math::mul(
                inverse_unit_quaternion(parent_world_trs.rotation),
                new_world_rotation));
            node.local = wz::math::transform(local_trs);
            return true;
        }

        void set_local_rotation(
            wz::math::Mat4& matrix,
            const wz::math::Quaternion& rotation) noexcept
        {
            const float sx = column_length(matrix, 0u);
            const float sy = column_length(matrix, 1u);
            const float sz = column_length(matrix, 2u);
            const wz::math::Mat4 r = wz::math::rotation(rotation);

            for (uint32_t row = 0; row < 3u; ++row) {
                matrix.m[0u * 4u + row] = r.m[0u * 4u + row] * sx;
                matrix.m[1u * 4u + row] = r.m[1u * 4u + row] * sy;
                matrix.m[2u * 4u + row] = r.m[2u * 4u + row] * sz;
            }
            matrix.m[3u] = 0.0f;
            matrix.m[7u] = 0.0f;
            matrix.m[11u] = 0.0f;
            matrix.m[15u] = 1.0f;
        }

        bool inverse_affine_point(
            const wz::math::Mat4& matrix,
            const wz::math::Vec3& point,
            wz::math::Vec3& out) noexcept
        {
            const float a00 = matrix.m[0];
            const float a01 = matrix.m[4];
            const float a02 = matrix.m[8];
            const float a10 = matrix.m[1];
            const float a11 = matrix.m[5];
            const float a12 = matrix.m[9];
            const float a20 = matrix.m[2];
            const float a21 = matrix.m[6];
            const float a22 = matrix.m[10];

            const float det =
                a00 * (a11 * a22 - a12 * a21)
                - a01 * (a10 * a22 - a12 * a20)
                + a02 * (a10 * a21 - a11 * a20);
            if (std::abs(det) <= 1e-8f) {
                return false;
            }

            const float inv_det = 1.0f / det;
            const float x = point.x - matrix.m[12];
            const float y = point.y - matrix.m[13];
            const float z = point.z - matrix.m[14];

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

        bool set_world_translation(
            wz::scene::SceneGraph& graph,
            wz::scene::TransformNode& node,
            wz::scene::RuntimeEntityId entity,
            const wz::math::Vec3& world_position) noexcept
        {
            const auto parent = wz::core::graph::parent(graph, entity);
            if (parent == wz::core::graph::INVALID_NODE) {
                node.local.m[12] = world_position.x;
                node.local.m[13] = world_position.y;
                node.local.m[14] = world_position.z;
                return true;
            }

            wz::math::Vec3 local_position{};
            if (!inverse_affine_point(
                    wz::core::graph::node_data(graph, parent).world,
                    world_position,
                    local_position))
            {
                return false;
            }

            node.local.m[12] = local_position.x;
            node.local.m[13] = local_position.y;
            node.local.m[14] = local_position.z;
            return true;
        }

        bool set_world_rotation(
            wz::scene::SceneGraph& graph,
            wz::scene::TransformNode& node,
            wz::scene::RuntimeEntityId entity,
            const wz::math::Quaternion& world_rotation) noexcept
        {
            wz::math::Transform local_trs{};
            if (!wz::math::decompose_trs(node.local, local_trs)) {
                return false;
            }

            const auto parent = wz::core::graph::parent(graph, entity);
            if (parent == wz::core::graph::INVALID_NODE) {
                local_trs.rotation = wz::math::normalize(world_rotation);
                node.local = wz::math::transform(local_trs);
                return true;
            }

            wz::math::Transform parent_world_trs{};
            if (!wz::math::decompose_trs(
                    wz::core::graph::node_data(graph, parent).world,
                    parent_world_trs))
            {
                return false;
            }

            local_trs.rotation = wz::math::normalize(wz::math::mul(
                inverse_unit_quaternion(parent_world_trs.rotation),
                world_rotation));
            node.local = wz::math::transform(local_trs);
            return true;
        }

        wz::engine::assets::MotionComponent* find_motion(
            wz::engine::assets::SceneInstance& scene,
            wz::scene::RuntimeEntityId entity) noexcept
        {
            for (auto& record : scene.motions) {
                if (record.node == entity) {
                    return &record.component;
                }
            }
            return nullptr;
        }

        wz::engine::assets::MotionComponent* ensure_motion(
            wz::engine::assets::SceneInstance& scene,
            wz::scene::RuntimeEntityId entity)
        {
            if (auto* motion = find_motion(scene, entity)) {
                return motion;
            }

            scene.motions.push_back({
                .node = entity,
                .component = wz::engine::assets::MotionComponent{},
            });
            return &scene.motions.back().component;
        }

        void mark_applied(
            wz::scene::RuntimeEntityId entity,
            uint32_t& applied,
            uint32_t& transform_applied,
            std::vector<wz::scene::RuntimeEntityId>* out_changed_entities)
        {
            ++applied;
            ++transform_applied;
            if (out_changed_entities) {
                out_changed_entities->push_back(entity);
            }
        }

        void sort_unique_changed(
            std::vector<wz::scene::RuntimeEntityId>* out_changed_entities)
        {
            if (!out_changed_entities) {
                return;
            }

            std::sort(
                out_changed_entities->begin(),
                out_changed_entities->end());
            out_changed_entities->erase(
                std::unique(
                    out_changed_entities->begin(),
                    out_changed_entities->end()),
                out_changed_entities->end());
        }

        bool entity_has_constraining_terrain(
            const wz::engine::assets::SceneInstance& scene,
            wz::scene::RuntimeEntityId entity) noexcept
        {
            for (const auto& record : scene.terrains) {
                if (record.node == entity
                    && record.component.constrain_movement)
                {
                    return true;
                }
            }
            // A movement-constraint Collision component (e.g. a scalar-field
            // heightfield collision with no TerrainAsset, issue #216) is an
            // equally valid constraint surface — build_collision_world feeds it
            // into terrain_constraint_surfaces, so it must pass this sample-time
            // filter too, or the surface is built and then silently discarded.
            for (const auto& record : scene.collisions) {
                if (record.node == entity
                    && record.component.constrain_movement)
                {
                    return true;
                }
            }
            return false;
        }

        struct TerrainConstraintSurface
        {
            bool found = false;
            wz::engine::collision::CollisionSurfaceSample sample{};
        };

        struct TerrainConstraintSupport
        {
            // Two footprints: `center` drives height/position (the sample under
            // the actor pivot), `highest` is the ring-max height fallback used
            // only when the center sample misses (actor straddling a terrain
            // edge). The averaged ring normal (normal_sum / sample_count) drives
            // orientation exactly as before.
            bool found = false;
            bool center_found = false;
            wz::engine::collision::CollisionSurfaceSample center{};
            wz::engine::collision::CollisionSurfaceSample highest{};
            wz::math::Vec3 normal_sum{};
            uint32_t sample_count = 0u;
        };

        bool has_terrain_constraint_surface(
            const wz::engine::collision::CollisionFrameStorage& collision,
            wz::scene::RuntimeEntityId entity) noexcept
        {
            for (const auto& entry : collision.terrain_constraint_surfaces) {
                if (entry.entity == entity && entry.enabled
                    && entry.resolved)
                {
                    return true;
                }
            }
            return false;
        }

        wz::engine::collision::CollisionWorldEntry
        world_entry_from_constraint_surface(
            const wz::engine::collision::TerrainConstraintSurfaceEntry& entry)
                noexcept
        {
            return wz::engine::collision::CollisionWorldEntry{
                .entity = entry.entity,
                .collision_asset = entry.collision_asset,
                .world_from_local = entry.world_from_local,
                .enabled = entry.enabled,
                .resolved = entry.resolved,
            };
        }

        TerrainConstraintSurface sample_constraint_surface(
            const wz::engine::assets::SceneInstance& scene,
            const wz::engine::collision::CollisionFrameStorage& collision,
            float world_x,
            float world_z) noexcept
        {
            bool found_surface = false;
            bool found_nearest_surface = false;
            wz::engine::collision::CollisionSurfaceSample best_sample{};
            wz::engine::collision::CollisionSurfaceSample best_nearest_sample{};

            auto sample_entry =
                [&](const wz::engine::collision::CollisionWorldEntry& entry)
            {
                wz::engine::collision::CollisionSurfaceSample sample{};
                if (wz::engine::collision::sample_terrain_surface(
                        entry,
                        world_x,
                        world_z,
                        sample)
                    && sample.hit)
                {
                    if (!found_surface
                        || sample.position.y > best_sample.position.y)
                    {
                        best_sample = sample;
                        found_surface = true;
                    }
                    return;
                }

                if (found_surface) {
                    return;
                }

                wz::engine::collision::CollisionSurfaceSample nearest_sample{};
                if (!wz::engine::collision::sample_nearest_terrain_surface(
                        entry,
                        world_x,
                        world_z,
                        nearest_sample)
                    || !nearest_sample.hit)
                {
                    return;
                }

                if (!found_nearest_surface
                    || nearest_sample.position.y
                        > best_nearest_sample.position.y)
                {
                    best_nearest_sample = nearest_sample;
                    found_nearest_surface = true;
                }
            };

            for (const auto& entry : collision.terrain_constraint_surfaces) {
                if (!entity_has_constraining_terrain(scene, entry.entity)) {
                    continue;
                }
                sample_entry(world_entry_from_constraint_surface(entry));
            }

            for (const auto& entry : collision.world) {
                if (!entity_has_constraining_terrain(scene, entry.entity)
                    || has_terrain_constraint_surface(collision, entry.entity))
                {
                    continue;
                }

                sample_entry(entry);
            }

            if (found_surface) {
                return TerrainConstraintSurface{
                    .found = true,
                    .sample = best_sample,
                };
            }
            if (found_nearest_surface) {
                return TerrainConstraintSurface{
                    .found = true,
                    .sample = best_nearest_sample,
                };
            }
            return {};
        }

        void add_support_sample(
            TerrainConstraintSupport& support,
            const wz::engine::collision::CollisionSurfaceSample& sample)
                noexcept
        {
            if (!sample.hit) {
                return;
            }
            if (!support.found
                || sample.position.y > support.highest.position.y)
            {
                support.highest = sample;
                support.found = true;
            }
            support.normal_sum.x += sample.normal.x;
            support.normal_sum.y += sample.normal.y;
            support.normal_sum.z += sample.normal.z;
            ++support.sample_count;
        }

        TerrainConstraintSupport sample_constraint_support(
            const wz::engine::assets::SceneInstance& scene,
            const wz::engine::collision::CollisionFrameStorage& collision,
            const wz::math::Vec3& actor_world_position,
            float footprint_radius) noexcept
        {
            TerrainConstraintSupport support{};
            const auto center = sample_constraint_surface(
                scene,
                collision,
                actor_world_position.x,
                actor_world_position.z);
            if (center.found && center.sample.hit) {
                support.center = center.sample;
                support.center_found = true;
                add_support_sample(support, center.sample);
            }

            if (footprint_radius <= 0.0f
                || !std::isfinite(footprint_radius))
            {
                return support;
            }

            constexpr wz::math::Vec3 kRingOffsets[] = {
                { 1.0f, 0.0f, 0.0f },
                { 0.70710677f, 0.0f, 0.70710677f },
                { 0.0f, 0.0f, 1.0f },
                { -0.70710677f, 0.0f, 0.70710677f },
                { -1.0f, 0.0f, 0.0f },
                { -0.70710677f, 0.0f, -0.70710677f },
                { 0.0f, 0.0f, -1.0f },
                { 0.70710677f, 0.0f, -0.70710677f },
            };

            for (const auto& offset : kRingOffsets) {
                const auto sample = sample_constraint_surface(
                    scene,
                    collision,
                    actor_world_position.x + offset.x * footprint_radius,
                    actor_world_position.z + offset.z * footprint_radius);
                if (sample.found) {
                    add_support_sample(support, sample.sample);
                }
            }

            return support;
        }
    }

    uint32_t apply_behavior_commands(
        wz::engine::assets::SceneInstance& scene,
        std::span<const BehaviorCommand> commands,
        std::vector<wz::scene::RuntimeEntityId>* out_changed_entities)
    {
        uint32_t applied = 0;
        uint32_t transform_applied = 0;
        if (out_changed_entities) {
            out_changed_entities->clear();
        }

        for (const BehaviorCommand& command : commands) {
            if (!entity_valid(scene, command.entity)) {
                continue;
            }

            // Scene graph node_data exposes a const view; command application is
            // the narrow mutation point that applies deferred behavior writes.
            auto& node = const_cast<wz::scene::TransformNode&>(
                wz::core::graph::node_data(
                    scene.storage.polytree,
                    command.entity));

            switch (command.kind) {
            case BehaviorCommandKind::AddLocalTranslation:
                node.local.m[12] += command.values[0];
                node.local.m[13] += command.values[1];
                node.local.m[14] += command.values[2];
                mark_applied(
                    command.entity,
                    applied,
                    transform_applied,
                    out_changed_entities);
                break;

            case BehaviorCommandKind::SetLocalTranslation:
                node.local.m[12] = command.values[0];
                node.local.m[13] = command.values[1];
                node.local.m[14] = command.values[2];
                mark_applied(
                    command.entity,
                    applied,
                    transform_applied,
                    out_changed_entities);
                break;

            case BehaviorCommandKind::AddWorldTranslation: {
                const wz::math::Vec3 world_position{
                    .x = node.world.m[12] + command.values[0],
                    .y = node.world.m[13] + command.values[1],
                    .z = node.world.m[14] + command.values[2],
                };
                if (set_world_translation(
                        scene.storage.polytree,
                        node,
                        command.entity,
                        world_position))
                {
                    mark_applied(
                        command.entity,
                        applied,
                        transform_applied,
                        out_changed_entities);
                }
                break;
            }

            case BehaviorCommandKind::SetWorldTranslation: {
                const wz::math::Vec3 world_position{
                    .x = command.values[0],
                    .y = command.values[1],
                    .z = command.values[2],
                };
                if (set_world_translation(
                        scene.storage.polytree,
                        node,
                        command.entity,
                        world_position))
                {
                    mark_applied(
                        command.entity,
                        applied,
                        transform_applied,
                        out_changed_entities);
                }
                break;
            }

            case BehaviorCommandKind::AddLocalScale:
                add_local_scale(
                    node.local,
                    command.values[0],
                    command.values[1],
                    command.values[2]);
                mark_applied(
                    command.entity,
                    applied,
                    transform_applied,
                    out_changed_entities);
                break;

            case BehaviorCommandKind::SetLocalScale:
                set_local_scale(
                    node.local,
                    command.values[0],
                    command.values[1],
                    command.values[2]);
                mark_applied(
                    command.entity,
                    applied,
                    transform_applied,
                    out_changed_entities);
                break;

            case BehaviorCommandKind::SetLocalRotation:
                set_local_rotation(
                    node.local,
                    wz::math::Quaternion{
                        command.values[0],
                        command.values[1],
                        command.values[2],
                        command.values[3],
                    });
                mark_applied(
                    command.entity,
                    applied,
                    transform_applied,
                    out_changed_entities);
                break;

            case BehaviorCommandKind::SetLinearVelocity: {
                auto* motion = ensure_motion(scene, command.entity);
                motion->linear_velocity[0] = command.values[0];
                motion->linear_velocity[1] = command.values[1];
                motion->linear_velocity[2] = command.values[2];
                motion->enabled = true;
                ++applied;
                break;
            }

            case BehaviorCommandKind::SetAngularVelocity: {
                auto* motion = ensure_motion(scene, command.entity);
                motion->angular_velocity[0] = command.values[0];
                motion->angular_velocity[1] = command.values[1];
                motion->angular_velocity[2] = command.values[2];
                motion->enabled = true;
                ++applied;
                break;
            }

            case BehaviorCommandKind::SetMotionSpace: {
                auto* motion = ensure_motion(scene, command.entity);
                if (!std::isfinite(command.values[0])
                    || command.values[0] < 0.0f)
                {
                    break;
                }
                const uint32_t raw =
                    static_cast<uint32_t>(command.values[0]);
                if (raw == static_cast<uint32_t>(
                        wz::engine::assets::SceneMotionSpace::Local))
                {
                    motion->space =
                        wz::engine::assets::SceneMotionSpace::Local;
                    motion->enabled = true;
                    ++applied;
                }
                else if (raw == static_cast<uint32_t>(
                             wz::engine::assets::SceneMotionSpace::World))
                {
                    motion->space =
                        wz::engine::assets::SceneMotionSpace::World;
                    motion->enabled = true;
                    ++applied;
                }
                break;
            }

            case BehaviorCommandKind::SetTerrainAlignmentRate: {
                // Write the runtime Motion field the terrain-constraint pass
                // reads to rate-limit alignment. Not a transform mutation, so it
                // does not mark the entity changed or trigger propagation.
                auto* motion = ensure_motion(scene, command.entity);
                if (motion && std::isfinite(command.values[0])) {
                    motion->terrain_alignment_rate = command.values[0];
                    ++applied;
                }
                break;
            }

            case BehaviorCommandKind::SetActiveCamera:
            case BehaviorCommandKind::PlaySound:
            case BehaviorCommandKind::StopSound:
            case BehaviorCommandKind::SetSoundGain:
            case BehaviorCommandKind::SetGrainParam:
            case BehaviorCommandKind::SpawnPrefab:
            case BehaviorCommandKind::SetRenderableParam:
            case BehaviorCommandKind::SetNodeVisible:
            case BehaviorCommandKind::SetNodeActive:
                // Not per-entity transform mutations: the host (WozzitsApp_v1)
                // reads these directly — camera selection / audio scheduler /
                // prefab graft / renderable-constant override / node visibility /
                // node active state. Ignored here.
                break;

            case BehaviorCommandKind::None:
                break;
            }
        }

        if (transform_applied != 0) {
            sort_unique_changed(out_changed_entities);
            wz::scene::propagate_all(scene.storage.polytree);
        }

        return applied;
    }

    uint32_t integrate_motion(
        wz::engine::assets::SceneInstance& scene,
        float delta_seconds,
        std::vector<wz::scene::RuntimeEntityId>* out_changed_entities)
    {
        uint32_t applied = 0;
        uint32_t transform_applied = 0;
        if (out_changed_entities) {
            out_changed_entities->clear();
        }
        if (delta_seconds <= 0.0f || !std::isfinite(delta_seconds)) {
            return 0;
        }

        for (auto& record : scene.motions) {
            if (!record.component.enabled || !entity_valid(scene, record.node)) {
                continue;
            }

            auto& node = const_cast<wz::scene::TransformNode&>(
                wz::core::graph::node_data(
                    scene.storage.polytree,
                    record.node));
            wz::math::Vec3 delta{
                .x = record.component.linear_velocity[0] * delta_seconds,
                .y = record.component.linear_velocity[1] * delta_seconds,
                .z = record.component.linear_velocity[2] * delta_seconds,
            };
            if (record.component.space
                == wz::engine::assets::SceneMotionSpace::Local)
            {
                delta = local_motion_to_world(
                    node.world,
                    delta.x,
                    delta.y,
                    delta.z);
            }
            if (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f) {
                const wz::math::Vec3 world_position{
                    .x = node.world.m[12] + delta.x,
                    .y = node.world.m[13] + delta.y,
                    .z = node.world.m[14] + delta.z,
                };
                if (set_world_translation(
                        scene.storage.polytree,
                        node,
                        record.node,
                        world_position))
                {
                    mark_applied(
                        record.node,
                        applied,
                        transform_applied,
                        out_changed_entities);
                }
            }

            if (integrate_angular_velocity(
                    scene.storage.polytree,
                    node,
                    record.node,
                    record.component,
                    delta_seconds))
            {
                mark_applied(
                    record.node,
                    applied,
                    transform_applied,
                    out_changed_entities);
            }
        }

        if (transform_applied != 0) {
            sort_unique_changed(out_changed_entities);
            wz::scene::propagate_all(scene.storage.polytree);
        }

        return applied;
    }

    uint32_t apply_terrain_constraints(
        wz::engine::assets::SceneInstance& scene,
        const wz::engine::collision::CollisionFrameStorage& collision,
        float delta_seconds,
        std::vector<wz::scene::RuntimeEntityId>* out_changed_entities)
    {
        uint32_t applied = 0;
        if (out_changed_entities) {
            out_changed_entities->clear();
        }

        for (const auto& record : scene.motions) {
            const auto& motion = record.component;
            if (!motion.enabled
                || !motion.terrain_constrained
                || !std::isfinite(motion.terrain_ride_height)
                || !entity_valid(scene, record.node))
            {
                continue;
            }

            auto& node = const_cast<wz::scene::TransformNode&>(
                wz::core::graph::node_data(
                    scene.storage.polytree,
                    record.node));
            const wz::math::Vec3 actor_world_position{
                .x = node.world.m[12],
                .y = node.world.m[13],
                .z = node.world.m[14],
            };

            auto support = sample_constraint_support(
                scene,
                collision,
                actor_world_position,
                motion.terrain_footprint_radius);
            if (!support.found) {
                continue;
            }

            wz::math::Vec3 support_normal = support.highest.normal;
            if (support.sample_count > 1u
                && normalize_vec3_checked(support.normal_sum))
            {
                support_normal = support.normal_sum;
            }

            // Height comes from the CENTER sample (the ground under the actor
            // pivot); the footprint ring only feeds the averaged orientation
            // normal above. On convex terrain a ring sample can catch a peak
            // higher than the ground under the actor, so a ring-max height would
            // levitate the actor. Fall back to the ring-max height only when the
            // center sample misses (actor straddling the terrain edge), which
            // preserves the existing edge-riding behavior.
            const float support_height = support.center_found
                ? support.center.position.y
                : support.highest.position.y;

            const wz::math::Vec3 constrained_world_position{
                .x = actor_world_position.x,
                .y = support_height + motion.terrain_ride_height,
                .z = actor_world_position.z,
            };
            bool changed = false;
            if (set_world_translation(
                    scene.storage.polytree,
                    node,
                    record.node,
                    constrained_world_position)
                && std::abs(
                    constrained_world_position.y - actor_world_position.y)
                    > 1e-6f)
            {
                changed = true;
            }

            // A behavior-set slew rate (radians/sec) rate-limits the swing; pass
            // rate * dt as the per-frame step cap (0 = use the strength blend).
            const bool rate_limited =
                std::isfinite(motion.terrain_alignment_rate)
                && motion.terrain_alignment_rate > 0.0f
                && std::isfinite(delta_seconds)
                && delta_seconds > 0.0f;
            const float max_step_radians = rate_limited
                ? motion.terrain_alignment_rate * delta_seconds
                : 0.0f;
            if (motion.terrain_align_to_surface
                && (rate_limited
                    || (std::isfinite(motion.terrain_alignment_strength)
                        && motion.terrain_alignment_strength > 0.0f)))
            {
                wz::math::Transform world_trs{};
                wz::math::Quaternion aligned_world_rotation{};
                if (wz::math::decompose_trs(node.world, world_trs)
                    && terrain_aligned_world_rotation(
                        world_trs.rotation,
                        support_normal,
                        motion.terrain_alignment_strength,
                        max_step_radians,
                        aligned_world_rotation)
                    && set_world_rotation(
                        scene.storage.polytree,
                        node,
                        record.node,
                        aligned_world_rotation))
                {
                    changed = true;
                }
            }

            if (changed) {
                ++applied;
                if (out_changed_entities) {
                    out_changed_entities->push_back(record.node);
                }
            }
        }

        if (applied != 0u) {
            sort_unique_changed(out_changed_entities);
            wz::scene::propagate_all(scene.storage.polytree);
        }

        return applied;
    }

    uint32_t integrate_linear_velocity(
        wz::engine::assets::SceneInstance& scene,
        float delta_seconds,
        std::vector<wz::scene::RuntimeEntityId>* out_changed_entities)
    {
        return integrate_motion(scene, delta_seconds, out_changed_entities);
    }
}
