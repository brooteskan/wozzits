#pragma once

// engine/behavior/behavior_module_api.h
//
// Small authoring layer for project behavior modules. This header stays on top
// of the C ABI, but gives module authors a simpler event-handler style.

#include <engine/behavior/behavior_plugin_abi.h>

#if defined(_WIN32)
#define WZ_BEHAVIOR_MODULE_EXPORT __declspec(dllexport)
#else
#define WZ_BEHAVIOR_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#define WZ_BEHAVIOR_MODULE(module_name, handler_fn)                         \
    extern "C" WZ_BEHAVIOR_MODULE_EXPORT uint8_t wz_register_behaviors(     \
        WzBehaviorPluginApi* api)                                           \
    {                                                                       \
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION                 \
            || !api->register_module)                                       \
        {                                                                   \
            return 0;                                                       \
        }                                                                   \
        return api->register_module(                                        \
            api->user,                                                      \
            module_name,                                                    \
            handler_fn,                                                     \
            nullptr);                                                       \
    }

// Transform command semantics:
// - Commands are buffered during behavior dispatch and applied afterward.
// - Local translation writes the matrix translation column directly.
// - Local scale changes local basis-column lengths and preserves directions.
// - Local rotation uses WzQuaternion {x, y, z, w}; it replaces rotation,
//   preserves translation, and preserves current basis-column scale.
// - Linear velocity is in world units per second and is integrated once per
//   frame after behavior commands are applied.
// - AddLocalRotation is intentionally not part of this V1 API.

static inline uint8_t wz_write_add_local_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_ADD_LOCAL_TRANSLATION,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_local_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_LOCAL_TRANSLATION,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_add_world_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_ADD_WORLD_TRANSLATION,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_world_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_WORLD_TRANSLATION,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_add_local_scale(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_ADD_LOCAL_SCALE,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_local_scale(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_LOCAL_SCALE,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_local_rotation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzQuaternion rotation)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_LOCAL_ROTATION,
        { rotation.x, rotation.y, rotation.z, rotation.w },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_linear_velocity(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_LINEAR_VELOCITY,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline float wz_delta_seconds(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->timing ? facts->timing->delta_seconds : 0.0f;
}

static inline uint64_t wz_frame_index(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->timing ? facts->timing->frame_index : 0u;
}

static inline void wz_log_info(
    const WzBehaviorFrameFacts* facts,
    const char* message)
{
    if (facts && facts->log_info && message) {
        facts->log_info(facts->log_user, message);
    }
}

static inline WzBehaviorEntityId wz_self(const WzBehaviorEvent* event)
{
    return event ? event->entity : WZ_INVALID_BEHAVIOR_ENTITY;
}

static inline WzBehaviorEventKind wz_event_kind(const WzBehaviorEvent* event)
{
    return event ? event->kind : WZ_EVENT_NONE;
}

static inline uint8_t wz_is_event(
    const WzBehaviorEvent* event,
    WzBehaviorEventKind kind)
{
    return wz_event_kind(event) == kind ? 1u : 0u;
}

static inline WzBehaviorEntityId wz_other(const WzBehaviorEvent* event)
{
    return event ? event->other : WZ_INVALID_BEHAVIOR_ENTITY;
}

static inline uint8_t wz_self_is_trigger(const WzBehaviorEvent* event)
{
    return event ? event->self_is_trigger : 0u;
}

static inline uint8_t wz_self_add_local_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_add_local_translation(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_local_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_local_translation(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_add_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_add_world_translation(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_world_translation(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_other_add_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_add_world_translation(facts, wz_other(event), x, y, z);
}

static inline uint8_t wz_other_set_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_world_translation(facts, wz_other(event), x, y, z);
}

static inline uint8_t wz_self_add_local_scale(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_add_local_scale(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_local_scale(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_local_scale(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_local_rotation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzQuaternion rotation)
{
    return wz_write_set_local_rotation(facts, wz_self(event), rotation);
}

static inline uint8_t wz_self_set_linear_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_linear_velocity(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_other_set_linear_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_linear_velocity(facts, wz_other(event), x, y, z);
}

static inline uint8_t wz_read_local_transform(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzMat4* out_transform)
{
    if (!facts || !facts->get_local_transform) {
        return 0;
    }
    return facts->get_local_transform(
        facts->transform_query_user,
        entity,
        out_transform);
}

static inline uint8_t wz_read_world_transform(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzMat4* out_transform)
{
    if (!facts || !facts->get_world_transform) {
        return 0;
    }
    return facts->get_world_transform(
        facts->transform_query_user,
        entity,
        out_transform);
}

static inline uint8_t wz_read_local_position(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzVec3* out_position)
{
    if (!facts || !facts->get_local_position) {
        return 0;
    }
    return facts->get_local_position(
        facts->transform_query_user,
        entity,
        out_position);
}

static inline uint8_t wz_read_world_position(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzVec3* out_position)
{
    if (!facts || !facts->get_world_position) {
        return 0;
    }
    return facts->get_world_position(
        facts->transform_query_user,
        entity,
        out_position);
}

static inline uint8_t wz_self_local_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform)
{
    return wz_read_local_transform(facts, wz_self(event), out_transform);
}

static inline uint8_t wz_self_world_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform)
{
    return wz_read_world_transform(facts, wz_self(event), out_transform);
}

static inline uint8_t wz_self_local_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position)
{
    return wz_read_local_position(facts, wz_self(event), out_position);
}

static inline uint8_t wz_self_world_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position)
{
    return wz_read_world_position(facts, wz_self(event), out_position);
}

static inline uint8_t wz_other_local_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform)
{
    return wz_read_local_transform(facts, wz_other(event), out_transform);
}

static inline uint8_t wz_other_world_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform)
{
    return wz_read_world_transform(facts, wz_other(event), out_transform);
}

static inline uint8_t wz_other_local_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position)
{
    return wz_read_local_position(facts, wz_other(event), out_position);
}

static inline uint8_t wz_other_world_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position)
{
    return wz_read_world_position(facts, wz_other(event), out_position);
}

static inline uint8_t wz_query_collision_surface_ray(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId surface_entity,
    WzVec3 origin,
    WzVec3 direction,
    float max_distance,
    WzSurfaceSample* out_sample)
{
    if (!facts || !facts->query_collision_surface_ray) {
        return 0;
    }
    return facts->query_collision_surface_ray(
        facts->collision_query_user,
        surface_entity,
        origin,
        direction,
        max_distance,
        out_sample);
}

static inline const char* wz_event_name(WzBehaviorEventKind kind)
{
    switch (kind) {
    case WZ_EVENT_FRAME_UPDATE:
        return "frame.update";
    case WZ_EVENT_SCENE_LOADED:
        return "scene.loaded";
    case WZ_EVENT_COLLISION_ENTER:
        return "collision.enter";
    case WZ_EVENT_COLLISION_STAY:
        return "collision.stay";
    case WZ_EVENT_COLLISION_EXIT:
        return "collision.exit";
    case WZ_EVENT_PROXIMITY_ENTER:
        return "proximity.enter";
    case WZ_EVENT_PROXIMITY_STAY:
        return "proximity.stay";
    case WZ_EVENT_PROXIMITY_EXIT:
        return "proximity.exit";
    default:
        return "unknown";
    }
}
