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
            return false;
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

            bool found_surface = false;
            wz::engine::collision::CollisionSurfaceSample best_sample{};
            for (const auto& entry : collision.world) {
                if (!entity_has_constraining_terrain(scene, entry.entity)) {
                    continue;
                }

                wz::engine::collision::CollisionSurfaceSample sample{};
                if (!wz::engine::collision::sample_terrain_surface(
                        entry,
                        actor_world_position.x,
                        actor_world_position.z,
                        sample)
                    || !sample.hit)
                {
                    continue;
                }

                if (!found_surface
                    || sample.position.y > best_sample.position.y)
                {
                    best_sample = sample;
                    found_surface = true;
                }
            }

            if (!found_surface) {
                continue;
            }

            const wz::math::Vec3 constrained_world_position{
                .x = actor_world_position.x,
                .y = best_sample.position.y + motion.terrain_ride_height,
                .z = actor_world_position.z,
            };
            if (std::abs(
                    constrained_world_position.y - actor_world_position.y)
                <= 1e-6f)
            {
                continue;
            }

            if (set_world_translation(
                    scene.storage.polytree,
                    node,
                    record.node,
                    constrained_world_position))
            {
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
