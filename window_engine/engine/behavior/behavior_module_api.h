#pragma once

// engine/behavior/behavior_module_api.h
//
// Small authoring layer for project behavior modules. This header stays on top
// of the C ABI, but gives module authors a simpler event-handler style.

#include <engine/behavior/behavior_plugin_abi.h>

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

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

#define WZ_BEHAVIOR_MODULE_EVENTS(                                          \
    module_name, handler_fn, event_channel_array)                           \
    extern "C" WZ_BEHAVIOR_MODULE_EXPORT uint8_t wz_register_behaviors(     \
        WzBehaviorPluginApi* api)                                           \
    {                                                                       \
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION                 \
            || !api->register_module_desc)                                  \
        {                                                                   \
            return 0;                                                       \
        }                                                                   \
        const WzBehaviorModuleDesc desc = {                                 \
            sizeof(WzBehaviorModuleDesc),                                   \
            module_name,                                                    \
            handler_fn,                                                     \
            nullptr,                                                        \
            event_channel_array,                                            \
            (uint32_t)(sizeof(event_channel_array)                          \
                / sizeof((event_channel_array)[0])),                        \
            nullptr,                                                        \
        };                                                                  \
        return api->register_module_desc(api->user, &desc);                 \
    }

#define WZ_BEHAVIOR_MODULE_INIT(                                            \
    module_name, init_fn, handler_fn, event_channel_array)                   \
    extern "C" WZ_BEHAVIOR_MODULE_EXPORT uint8_t wz_register_behaviors(     \
        WzBehaviorPluginApi* api)                                           \
    {                                                                       \
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION                 \
            || !api->register_module_desc)                                  \
        {                                                                   \
            return 0;                                                       \
        }                                                                   \
        const WzBehaviorModuleDesc desc = {                                 \
            sizeof(WzBehaviorModuleDesc),                                   \
            module_name,                                                    \
            handler_fn,                                                     \
            init_fn,                                                        \
            event_channel_array,                                            \
            (uint32_t)(sizeof(event_channel_array)                          \
                / sizeof((event_channel_array)[0])),                        \
            nullptr,                                                        \
        };                                                                  \
        return api->register_module_desc(api->user, &desc);                 \
    }

static inline uint8_t wz_key_down(
    const WzBehaviorFrameFacts* facts,
    uint32_t key)
{
    return facts && facts->input && key < 256u
        ? facts->input->keyboard_down[key]
        : 0u;
}

static inline uint8_t wz_key_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t key)
{
    return facts && facts->input && key < 256u
        ? facts->input->keyboard_pressed[key]
        : 0u;
}

static inline uint8_t wz_key_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t key)
{
    return facts && facts->input && key < 256u
        ? facts->input->keyboard_released[key]
        : 0u;
}

static inline uint8_t wz_mouse_button_down(
    const WzBehaviorFrameFacts* facts,
    uint32_t button)
{
    return facts && facts->input && button < 3u
        ? facts->input->mouse_down[button]
        : 0u;
}

static inline uint8_t wz_mouse_button_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t button)
{
    return facts && facts->input && button < 3u
        ? facts->input->mouse_pressed[button]
        : 0u;
}

static inline uint8_t wz_mouse_button_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t button)
{
    return facts && facts->input && button < 3u
        ? facts->input->mouse_released[button]
        : 0u;
}

static inline int32_t wz_mouse_x(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->mouse_x : 0;
}

static inline int32_t wz_mouse_y(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->mouse_y : 0;
}

static inline int32_t wz_mouse_dx(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->mouse_dx : 0;
}

static inline int32_t wz_mouse_dy(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->mouse_dy : 0;
}

static inline uint8_t wz_window_focused(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->window_focused : 0u;
}

static inline int32_t wz_window_width(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->window_width : 0;
}

static inline int32_t wz_window_height(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->window_height : 0;
}

static inline uint8_t wz_controller_count(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->controller_count : 0u;
}

static inline uint8_t wz_controller_connected(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        ? facts->input->controller_connected[controller]
        : 0u;
}

static inline uint8_t wz_controller_connected_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        ? facts->input->controller_connected_pressed[controller]
        : 0u;
}

static inline uint8_t wz_controller_connected_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        ? facts->input->controller_connected_released[controller]
        : 0u;
}

static inline float wz_controller_axis(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t axis)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        && axis < WZ_CONTROLLER_AXIS_COUNT
        ? facts->input->controller_axes[controller][axis]
        : 0.0f;
}

static inline uint8_t wz_controller_button_down(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t button)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        && button < WZ_CONTROLLER_BUTTON_COUNT
        ? facts->input->controller_buttons[controller][button]
        : 0u;
}

static inline uint8_t wz_controller_button_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t button)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        && button < WZ_CONTROLLER_BUTTON_COUNT
        ? facts->input->controller_buttons_pressed[controller][button]
        : 0u;
}

static inline uint8_t wz_controller_button_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t button)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        && button < WZ_CONTROLLER_BUTTON_COUNT
        ? facts->input->controller_buttons_released[controller][button]
        : 0u;
}

static inline uint8_t wz_input_wasd_axis(
    const WzBehaviorFrameFacts* facts,
    WzVec3* out_axis)
{
    if (!facts || !facts->input || !out_axis) {
        return 0;
    }

    out_axis->x =
        (wz_key_down(facts, WZ_KEY_D) ? 1.0f : 0.0f)
        - (wz_key_down(facts, WZ_KEY_A) ? 1.0f : 0.0f);
    out_axis->y = 0.0f;
    out_axis->z =
        (wz_key_down(facts, WZ_KEY_W) ? 1.0f : 0.0f)
        - (wz_key_down(facts, WZ_KEY_S) ? 1.0f : 0.0f);

    const float length =
        sqrtf(out_axis->x * out_axis->x
            + out_axis->z * out_axis->z);
    if (length > 1.0f) {
        const float inv_length = 1.0f / length;
        out_axis->x *= inv_length;
        out_axis->z *= inv_length;
    }
    return 1;
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

static inline uint8_t wz_write_set_angular_velocity(
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
        WZ_BEHAVIOR_COMMAND_SET_ANGULAR_VELOCITY,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_motion_space(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzBehaviorMotionSpace space)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_MOTION_SPACE,
        { (float)space, 0.0f, 0.0f, 0.0f },
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

static inline void wz_log_infof(
    const WzBehaviorFrameFacts* facts,
    const char* format,
    ...)
{
    if (!facts || !facts->log_info || !format) {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (written < 0) {
        return;
    }
    message[sizeof(message) - 1u] = '\0';
    wz_log_info(facts, message);
}

static inline void wz_log_info(
    const WzBehaviorInitFacts* facts,
    const char* message)
{
    if (facts && facts->log_info && message) {
        facts->log_info(facts->log_user, message);
    }
}

static inline void wz_log_infof(
    const WzBehaviorInitFacts* facts,
    const char* format,
    ...)
{
    if (!facts || !facts->log_info || !format) {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (written < 0) {
        return;
    }
    message[sizeof(message) - 1u] = '\0';
    wz_log_info(facts, message);
}

static inline void wz_log_info(decltype(nullptr), const char*)
{
}

static inline void wz_log_infof(decltype(nullptr), const char*, ...)
{
}

static inline void* wz_alloc_instance_state(
    const WzBehaviorInitFacts* facts,
    uint32_t size,
    uint32_t alignment)
{
    return facts && facts->alloc_instance_state
        ? facts->alloc_instance_state(
            facts->behavior_state_user,
            size,
            alignment)
        : nullptr;
}

static inline void* wz_alloc_instance_state_desc(
    const WzBehaviorInitFacts* facts,
    const WzBehaviorStateDesc* desc)
{
    return facts && facts->alloc_instance_state_desc
        ? facts->alloc_instance_state_desc(
            facts->behavior_state_user,
            desc)
        : nullptr;
}

static inline void* wz_get_instance_state(
    const WzBehaviorInitFacts* facts)
{
    return facts && facts->get_instance_state
        ? facts->get_instance_state(facts->behavior_state_user)
        : nullptr;
}

static inline void* wz_get_instance_state(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->get_instance_state
        ? facts->get_instance_state(facts->behavior_state_user)
        : nullptr;
}

static inline void* wz_create_shared_state(
    const WzBehaviorInitFacts* facts,
    const char* key,
    uint32_t size,
    uint32_t alignment)
{
    return facts && facts->create_shared_state
        ? facts->create_shared_state(
            facts->behavior_state_user,
            key,
            size,
            alignment)
        : nullptr;
}

static inline void* wz_create_shared_state_desc(
    const WzBehaviorInitFacts* facts,
    const char* key,
    const WzBehaviorStateDesc* desc)
{
    return facts && facts->create_shared_state_desc
        ? facts->create_shared_state_desc(
            facts->behavior_state_user,
            key,
            desc)
        : nullptr;
}

static inline void* wz_find_shared_state(
    const WzBehaviorInitFacts* facts,
    const char* key)
{
    return facts && facts->find_shared_state
        ? facts->find_shared_state(facts->behavior_state_user, key)
        : nullptr;
}

static inline void* wz_find_shared_state(
    const WzBehaviorFrameFacts* facts,
    const char* key)
{
    return facts && facts->find_shared_state
        ? facts->find_shared_state(facts->behavior_state_user, key)
        : nullptr;
}

static inline void* wz_find_shared_state(decltype(nullptr), const char*)
{
    return nullptr;
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

static inline uint32_t wz_input_event_key(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->key
        : WZ_INPUT_EVENT_INVALID_VALUE;
}

static inline uint32_t wz_input_event_mouse_button(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->button
        : WZ_INPUT_EVENT_INVALID_VALUE;
}

static inline uint32_t wz_input_event_controller(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->controller
        : WZ_INPUT_EVENT_INVALID_VALUE;
}

static inline uint32_t wz_input_event_controller_button(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->button
        : WZ_INPUT_EVENT_INVALID_VALUE;
}

static inline uint32_t wz_input_event_controller_axis(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->axis
        : WZ_INPUT_EVENT_INVALID_VALUE;
}

static inline float wz_input_event_controller_axis_value(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->value
        : 0.0f;
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

static inline uint8_t wz_self_set_angular_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_angular_velocity(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_motion_space(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzBehaviorMotionSpace space)
{
    return wz_write_set_motion_space(facts, wz_self(event), space);
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

static inline uint8_t wz_other_set_angular_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_angular_velocity(facts, wz_other(event), x, y, z);
}

static inline uint8_t wz_other_set_motion_space(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzBehaviorMotionSpace space)
{
    return wz_write_set_motion_space(facts, wz_other(event), space);
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

static inline uint8_t wz_read_local_transform(
    const WzBehaviorInitFacts* facts,
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
    const WzBehaviorInitFacts* facts,
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
    const WzBehaviorInitFacts* facts,
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
    const WzBehaviorInitFacts* facts,
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

static inline uint8_t wz_vector_between_world_positions(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId from_entity,
    WzBehaviorEntityId to_entity,
    WzVec3* out_vector)
{
    if (!out_vector) {
        return 0;
    }

    WzVec3 from{};
    WzVec3 to{};
    if (!wz_read_world_position(facts, from_entity, &from)
        || !wz_read_world_position(facts, to_entity, &to))
    {
        return 0;
    }

    out_vector->x = to.x - from.x;
    out_vector->y = to.y - from.y;
    out_vector->z = to.z - from.z;
    return 1;
}

static inline uint8_t wz_vector_self_to_other(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_vector)
{
    return wz_vector_between_world_positions(
        facts,
        wz_self(event),
        wz_other(event),
        out_vector);
}

static inline uint8_t wz_distance_between_world_positions(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId from_entity,
    WzBehaviorEntityId to_entity,
    float* out_distance)
{
    if (!out_distance) {
        return 0;
    }

    WzVec3 vector{};
    if (!wz_vector_between_world_positions(
            facts,
            from_entity,
            to_entity,
            &vector))
    {
        return 0;
    }

    *out_distance =
        sqrtf(vector.x * vector.x
            + vector.y * vector.y
            + vector.z * vector.z);
    return 1;
}

static inline uint8_t wz_distance_self_to_other(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float* out_distance)
{
    return wz_distance_between_world_positions(
        facts,
        wz_self(event),
        wz_other(event),
        out_distance);
}

static inline uint8_t wz_direction_between_world_positions(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId from_entity,
    WzBehaviorEntityId to_entity,
    WzVec3* out_direction)
{
    if (!out_direction) {
        return 0;
    }

    WzVec3 vector{};
    if (!wz_vector_between_world_positions(
            facts,
            from_entity,
            to_entity,
            &vector))
    {
        return 0;
    }

    const float length =
        sqrtf(vector.x * vector.x
            + vector.y * vector.y
            + vector.z * vector.z);
    if (length <= 0.000001f) {
        out_direction->x = 0.0f;
        out_direction->y = 0.0f;
        out_direction->z = 0.0f;
        return 0;
    }

    const float inv_length = 1.0f / length;
    out_direction->x = vector.x * inv_length;
    out_direction->y = vector.y * inv_length;
    out_direction->z = vector.z * inv_length;
    return 1;
}

static inline uint8_t wz_direction_self_to_other(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_direction)
{
    return wz_direction_between_world_positions(
        facts,
        wz_self(event),
        wz_other(event),
        out_direction);
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

static inline uint8_t wz_sample_terrain_surface(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId terrain_entity,
    float world_x,
    float world_z,
    WzSurfaceSample* out_sample)
{
    if (!facts || !facts->sample_terrain_surface) {
        return 0;
    }
    return facts->sample_terrain_surface(
        facts->collision_query_user,
        terrain_entity,
        world_x,
        world_z,
        out_sample);
}

static inline uint8_t wz_find_entity_by_name(
    const WzBehaviorFrameFacts* facts,
    const char* name,
    WzBehaviorEntityId* out_entity)
{
    if (!facts || !facts->find_entity_by_name) {
        return 0;
    }
    return facts->find_entity_by_name(
        facts->scene_query_user,
        name,
        out_entity);
}

static inline uint8_t wz_find_entity_by_authored_id(
    const WzBehaviorFrameFacts* facts,
    const char* authored_id,
    WzBehaviorEntityId* out_entity)
{
    if (!facts || !facts->find_entity_by_authored_id) {
        return 0;
    }
    return facts->find_entity_by_authored_id(
        facts->scene_query_user,
        authored_id,
        out_entity);
}

static inline uint8_t wz_config_bool(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    uint8_t* out_value)
{
    if (!facts || !facts->get_config_bool) {
        return 0;
    }
    return facts->get_config_bool(
        facts->behavior_config_user,
        key,
        out_value);
}

static inline uint8_t wz_config_number(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    double* out_value)
{
    if (!facts || !facts->get_config_number) {
        return 0;
    }
    return facts->get_config_number(
        facts->behavior_config_user,
        key,
        out_value);
}

static inline uint8_t wz_config_float(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    float* out_value)
{
    if (!out_value) {
        return 0;
    }

    double value = 0.0;
    if (!wz_config_number(facts, key, &value)) {
        return 0;
    }

    *out_value = (float)value;
    return 1;
}

static inline uint8_t wz_config_string(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    char* out_buffer,
    uint32_t buffer_size,
    uint32_t* out_required_size)
{
    if (!facts || !facts->get_config_string) {
        return 0;
    }
    return facts->get_config_string(
        facts->behavior_config_user,
        key,
        out_buffer,
        buffer_size,
        out_required_size);
}

static inline uint8_t wz_find_entity_by_name(
    const WzBehaviorInitFacts* facts,
    const char* name,
    WzBehaviorEntityId* out_entity)
{
    if (!facts || !facts->find_entity_by_name) {
        return 0;
    }
    return facts->find_entity_by_name(
        facts->scene_query_user,
        name,
        out_entity);
}

static inline uint8_t wz_find_entity_by_authored_id(
    const WzBehaviorInitFacts* facts,
    const char* authored_id,
    WzBehaviorEntityId* out_entity)
{
    if (!facts || !facts->find_entity_by_authored_id) {
        return 0;
    }
    return facts->find_entity_by_authored_id(
        facts->scene_query_user,
        authored_id,
        out_entity);
}

static inline uint8_t wz_config_bool(
    const WzBehaviorInitFacts* facts,
    const char* key,
    uint8_t* out_value)
{
    if (!facts || !facts->get_config_bool) {
        return 0;
    }
    return facts->get_config_bool(
        facts->behavior_config_user,
        key,
        out_value);
}

static inline uint8_t wz_config_number(
    const WzBehaviorInitFacts* facts,
    const char* key,
    double* out_value)
{
    if (!facts || !facts->get_config_number) {
        return 0;
    }
    return facts->get_config_number(
        facts->behavior_config_user,
        key,
        out_value);
}

static inline uint8_t wz_config_float(
    const WzBehaviorInitFacts* facts,
    const char* key,
    float* out_value)
{
    if (!out_value) {
        return 0;
    }

    double value = 0.0;
    if (!wz_config_number(facts, key, &value)) {
        return 0;
    }

    *out_value = (float)value;
    return 1;
}

static inline uint8_t wz_config_string(
    const WzBehaviorInitFacts* facts,
    const char* key,
    char* out_buffer,
    uint32_t buffer_size,
    uint32_t* out_required_size)
{
    if (!facts || !facts->get_config_string) {
        return 0;
    }
    return facts->get_config_string(
        facts->behavior_config_user,
        key,
        out_buffer,
        buffer_size,
        out_required_size);
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
    case WZ_EVENT_INPUT_KEY_PRESSED:
        return "input.key.pressed";
    case WZ_EVENT_INPUT_KEY_RELEASED:
        return "input.key.released";
    case WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED:
        return "input.mouse_button.pressed";
    case WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED:
        return "input.mouse_button.released";
    case WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED:
        return "input.controller_button.pressed";
    case WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED:
        return "input.controller_button.released";
    case WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED:
        return "input.controller_axis.changed";
    default:
        return "unknown";
    }
}
