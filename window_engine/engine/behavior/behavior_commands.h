#pragma once

// engine/behavior/behavior_commands.h

#include <scene/scene_ecs.h>

#include <cstdint>
#include <vector>

namespace wz::engine::behavior
{
    enum class BehaviorCommandKind : uint8_t
    {
        None = 0,
        AddLocalTranslation,
        SetLocalTranslation,
        AddLocalScale,
        SetLocalScale,
        SetLocalRotation,
        AddWorldTranslation,
        SetWorldTranslation,
        SetLinearVelocity,
    };

    struct BehaviorCommand
    {
        wz::scene::RuntimeEntityId entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        BehaviorCommandKind kind = BehaviorCommandKind::None;
        float values[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct BehaviorCommandBuffer
    {
        std::vector<BehaviorCommand> commands;

        void clear() { commands.clear(); }

        void add_local_translation(
            wz::scene::RuntimeEntityId entity,
            float x,
            float y,
            float z)
        {
            commands.push_back({
                .entity = entity,
                .kind = BehaviorCommandKind::AddLocalTranslation,
                .values = { x, y, z, 0.0f },
            });
        }

        void set_local_translation(
            wz::scene::RuntimeEntityId entity,
            float x,
            float y,
            float z)
        {
            commands.push_back({
                .entity = entity,
                .kind = BehaviorCommandKind::SetLocalTranslation,
                .values = { x, y, z, 0.0f },
            });
        }

        void add_world_translation(
            wz::scene::RuntimeEntityId entity,
            float x,
            float y,
            float z)
        {
            commands.push_back({
                .entity = entity,
                .kind = BehaviorCommandKind::AddWorldTranslation,
                .values = { x, y, z, 0.0f },
            });
        }

        void set_world_translation(
            wz::scene::RuntimeEntityId entity,
            float x,
            float y,
            float z)
        {
            commands.push_back({
                .entity = entity,
                .kind = BehaviorCommandKind::SetWorldTranslation,
                .values = { x, y, z, 0.0f },
            });
        }

        void add_local_scale(
            wz::scene::RuntimeEntityId entity,
            float x,
            float y,
            float z)
        {
            commands.push_back({
                .entity = entity,
                .kind = BehaviorCommandKind::AddLocalScale,
                .values = { x, y, z, 0.0f },
            });
        }

        void set_local_scale(
            wz::scene::RuntimeEntityId entity,
            float x,
            float y,
            float z)
        {
            commands.push_back({
                .entity = entity,
                .kind = BehaviorCommandKind::SetLocalScale,
                .values = { x, y, z, 0.0f },
            });
        }

        void set_local_rotation(
            wz::scene::RuntimeEntityId entity,
            float x,
            float y,
            float z,
            float w)
        {
            commands.push_back({
                .entity = entity,
                .kind = BehaviorCommandKind::SetLocalRotation,
                .values = { x, y, z, w },
            });
        }

        void set_linear_velocity(
            wz::scene::RuntimeEntityId entity,
            float x,
            float y,
            float z)
        {
            commands.push_back({
                .entity = entity,
                .kind = BehaviorCommandKind::SetLinearVelocity,
                .values = { x, y, z, 0.0f },
            });
        }
    };
}
