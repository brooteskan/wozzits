#pragma once

// engine/behavior/behavior_commands.h

#include <engine/assets/scene/scene_asset_data.h>
#include <scene/scene_ecs.h>

#include <cstdint>
#include <string>
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
        // Make `entity` the runtime's active camera. Not applied by
        // apply_behavior_commands (it does not mutate the entity); the host
        // (WozzitsApp_v1) reads the entity's transform + SceneCameraAsset and
        // points the runtime camera at it. See WZ_BEHAVIOR_COMMAND_SET_ACTIVE_CAMERA.
        SetActiveCamera,
        // Audio (audio-track item 9). Not applied by apply_behavior_commands; the
        // host resolves `entity` → its AudioSource and posts to the audio
        // scheduler. See WZ_BEHAVIOR_COMMAND_PLAY_SOUND / STOP_SOUND / SET_SOUND_GAIN.
        PlaySound,
        StopSound,
        SetSoundGain,
        // Ramp a live grain-cloud param on `entity`'s AudioSource. Also host-
        // handled (not applied here). values[0] = WZ_GRAIN_PARAM_* ordinal,
        // values[1] = target, values[2] = ramp frames.
        SetGrainParam,
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

    // A behavior-issued set-renderable request, already resolved to the target
    // node's authored scene-node id (the apply path's currency) paired with the
    // authored asset-graph node id to bind (0 clears the renderable).
    struct BehaviorSetRenderableRequest
    {
        wz::scene::AuthoredEntityId node_id;
        uint64_t asset_graph_node_id = 0u;
    };

    // A behavior-issued reparent request, already resolved to authored
    // scene-node ids (the apply path's currency): move `node_id` under
    // `new_parent_id`. An empty `new_parent_id` detaches the node to the TOP
    // LEVEL (mirrors WozzitsApp_v1::reparent_node's empty-parent contract).
    struct BehaviorReparentRequest
    {
        wz::scene::AuthoredEntityId node_id;
        wz::scene::AuthoredEntityId new_parent_id;
    };

    // A behavior-issued add/remove optional-component request, already resolved
    // to the target node's authored scene-node id (the apply path's currency)
    // paired with the component kind token to add/remove ("camera" |
    // "proximity" | "collision" | "motion"). The kind is carried as an owned
    // std::string: the behavior passes a const char* valid only during its
    // call, but this request is drained at the frame boundary, so the adapter
    // copies it here rather than storing the transient pointer.
    struct BehaviorComponentRequest
    {
        wz::scene::AuthoredEntityId node_id;
        std::string kind;
    };

    // Deferred runtime-authoring requests issued by behaviors during dispatch
    // (#204). Distinct from BehaviorCommandBuffer (which carries transform/
    // velocity commands the dispatch loop applies to the running instance):
    // these are CHEAP live scene-ECS authoring edits queued mid-dispatch and
    // drained at the frame boundary AFTER the dispatch loop finishes, through
    // the same WozzitsApp_v1 apply methods the host's add_child/remove/
    // set_node_renderable_asset use. Entries are already resolved to authored
    // scene-node ids (the apply path's currency), so the buffer is independent
    // of runtime entity ids that a structural rebuild would invalidate. v1
    // carries cheap live-ECS edits only: spawn-child (a child node added under
    // each parent), remove-node (each target node removed), and set-renderable
    // (each node's preferred asset-graph renderable set); nothing heavier (e.g.
    // an asset-DAG recompile) is exposed. reparent (each node moved under a new
    // parent, empty parent = top level) and add/remove-component (each node's
    // optional component added/removed) join the same cheap-edit family.
    struct BehaviorAuthoringBuffer
    {
        std::vector<wz::scene::AuthoredEntityId> spawn_child_parents;
        std::vector<wz::scene::AuthoredEntityId> remove_node_targets;
        std::vector<BehaviorSetRenderableRequest> set_renderable_requests;
        std::vector<BehaviorReparentRequest> reparent_requests;
        std::vector<BehaviorComponentRequest> add_component_requests;
        std::vector<BehaviorComponentRequest> remove_component_requests;

        void clear()
        {
            spawn_child_parents.clear();
            remove_node_targets.clear();
            set_renderable_requests.clear();
            reparent_requests.clear();
            add_component_requests.clear();
            remove_component_requests.clear();
        }

        [[nodiscard]] bool empty() const
        {
            return spawn_child_parents.empty()
                && remove_node_targets.empty()
                && set_renderable_requests.empty()
                && reparent_requests.empty()
                && add_component_requests.empty()
                && remove_component_requests.empty();
        }
    };
}
