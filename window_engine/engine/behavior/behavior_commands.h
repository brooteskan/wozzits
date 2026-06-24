#pragma once

// engine/behavior/behavior_commands.h

#include <engine/assets/scene/scene_asset_data.h>
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
        SetAngularVelocity,
        SetMotionSpace,
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

        void set_angular_velocity(
            wz::scene::RuntimeEntityId entity,
            float x,
            float y,
            float z)
        {
            commands.push_back({
                .entity = entity,
                .kind = BehaviorCommandKind::SetAngularVelocity,
                .values = { x, y, z, 0.0f },
            });
        }

        void set_motion_space(
            wz::scene::RuntimeEntityId entity,
            wz::engine::assets::SceneMotionSpace space)
        {
            commands.push_back({
                .entity = entity,
                .kind = BehaviorCommandKind::SetMotionSpace,
                .values = {
                    static_cast<float>(static_cast<uint8_t>(space)),
                    0.0f,
                    0.0f,
                    0.0f },
            });
        }
    };

    // Deferred runtime-authoring requests issued by behaviors during dispatch
    // (#204). Distinct from BehaviorCommandBuffer (which carries transform/
    // velocity commands the dispatch loop applies to the running instance):
    // these are CHEAP live scene-ECS authoring edits queued mid-dispatch and
    // drained at the frame boundary AFTER the dispatch loop finishes, through
    // the same WozzitsApp_v1 apply method the host's add_child uses. Entries are
    // already resolved to the parent's authored scene-node id (the apply path's
    // currency), so the buffer is independent of runtime entity ids that a
    // structural rebuild would invalidate. v1 carries only spawn-child (a child
    // node added under each parent); nothing heavier is exposed.
    struct BehaviorAuthoringBuffer
    {
        std::vector<wz::scene::AuthoredEntityId> spawn_child_parents;

        void clear() { spawn_child_parents.clear(); }

        [[nodiscard]] bool empty() const
        {
            return spawn_child_parents.empty();
        }
    };
}
