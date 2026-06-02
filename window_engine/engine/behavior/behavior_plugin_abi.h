#pragma once

// engine/behavior/behavior_plugin_abi.h
//
// C-compatible behavior plugin ABI. Keep this header free of STL and engine
// implementation types; dynamic modules should eventually be able to include
// this file without depending on engine internals.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WZ_BEHAVIOR_ABI_VERSION 3u
#define WZ_BEHAVIOR_PLUGIN_REGISTER_SYMBOL "wz_register_behaviors"

typedef uint32_t WzBehaviorEntityId;

typedef struct WzVec3
{
    float x;
    float y;
    float z;
} WzVec3;

typedef struct WzMat4
{
    float m[16];
} WzMat4;

typedef struct WzQuaternion
{
    float x;
    float y;
    float z;
    float w;
} WzQuaternion;

typedef uint32_t WzCollisionEventKind;
enum
{
    WZ_COLLISION_EVENT_ENTER = 1u,
    WZ_COLLISION_EVENT_STAY = 2u,
    WZ_COLLISION_EVENT_EXIT = 3u,
};

typedef uint32_t WzBehaviorEventKind;
enum
{
    WZ_EVENT_NONE = 0u,
    WZ_EVENT_FRAME_UPDATE = 1u,
    WZ_EVENT_SCENE_LOADED = 2u,
    WZ_EVENT_COLLISION_ENTER = 100u,
    WZ_EVENT_COLLISION_STAY = 101u,
    WZ_EVENT_COLLISION_EXIT = 102u,
    WZ_EVENT_PROXIMITY_ENTER = 200u,
    WZ_EVENT_PROXIMITY_STAY = 201u,
    WZ_EVENT_PROXIMITY_EXIT = 202u,
};

enum
{
    WZ_INVALID_BEHAVIOR_ENTITY = 0xffffffffu,
};

typedef struct WzBehaviorEvent
{
    WzBehaviorEventKind kind;
    WzBehaviorEntityId entity;
    WzBehaviorEntityId other;
    uint8_t self_is_trigger;
} WzBehaviorEvent;

typedef struct WzCollisionEntityEvent
{
    WzBehaviorEntityId entity;
    WzBehaviorEntityId other;
    WzCollisionEventKind kind;
    uint8_t self_is_trigger;
} WzCollisionEntityEvent;

typedef uint8_t (*WzReadCollisionEntityEventFn)(
    void* user,
    uint32_t index,
    WzCollisionEntityEvent* out_event);

typedef struct WzCollisionEntityEventView
{
    void* user;
    uint32_t count;
    WzReadCollisionEntityEventFn read;
} WzCollisionEntityEventView;

typedef struct WzSurfaceSample
{
    uint8_t hit;
    WzBehaviorEntityId surface_entity;
    WzVec3 position;
    WzVec3 normal;
} WzSurfaceSample;

typedef struct WzInputStateView
{
    uint8_t keyboard_down[256];
    uint8_t keyboard_pressed[256];
    uint8_t keyboard_released[256];

    int32_t mouse_x;
    int32_t mouse_y;
    int32_t mouse_dx;
    int32_t mouse_dy;
    uint8_t mouse_down[3];
    uint8_t mouse_pressed[3];
    uint8_t mouse_released[3];

    uint8_t window_focused;
    int32_t window_width;
    int32_t window_height;

    uint8_t controller_connected;
    float controller_axes[8];
    uint8_t controller_buttons[16];
} WzInputStateView;

typedef uint32_t WzBehaviorCommandKind;
enum
{
    WZ_BEHAVIOR_COMMAND_NONE = 0u,
    WZ_BEHAVIOR_COMMAND_ADD_LOCAL_TRANSLATION = 1u,
    WZ_BEHAVIOR_COMMAND_SET_LOCAL_TRANSLATION = 2u,
    WZ_BEHAVIOR_COMMAND_ADD_LOCAL_SCALE = 3u,
    WZ_BEHAVIOR_COMMAND_SET_LOCAL_SCALE = 4u,
    /* values = { x, y, z, w } matching WzQuaternion. */
    WZ_BEHAVIOR_COMMAND_SET_LOCAL_ROTATION = 5u,
    WZ_BEHAVIOR_COMMAND_ADD_WORLD_TRANSLATION = 6u,
    WZ_BEHAVIOR_COMMAND_SET_WORLD_TRANSLATION = 7u,
    WZ_BEHAVIOR_COMMAND_SET_LINEAR_VELOCITY = 8u,
};

typedef struct WzBehaviorCommand
{
    WzBehaviorEntityId entity;
    WzBehaviorCommandKind kind;
    float values[4];
} WzBehaviorCommand;

typedef uint8_t (*WzWriteBehaviorCommandFn)(
    void* user,
    const WzBehaviorCommand* command);

typedef void (*WzBehaviorLogFn)(
    void* user,
    const char* message);

typedef uint8_t (*WzGetBehaviorTransformFn)(
    void* user,
    WzBehaviorEntityId entity,
    WzMat4* out_transform);

typedef uint8_t (*WzGetBehaviorPositionFn)(
    void* user,
    WzBehaviorEntityId entity,
    WzVec3* out_position);

typedef uint8_t (*WzQueryBehaviorCollisionSurfaceRayFn)(
    void* user,
    WzBehaviorEntityId surface_entity,
    WzVec3 origin,
    WzVec3 direction,
    float max_distance,
    WzSurfaceSample* out_sample);

typedef struct WzFrameTiming
{
    float delta_seconds;
    double elapsed_seconds;
    uint64_t frame_index;
} WzFrameTiming;

typedef struct WzBehaviorFrameFacts
{
    const WzInputStateView* input;
    WzCollisionEntityEventView collision_events;

    void* transform_query_user;
    WzGetBehaviorTransformFn get_local_transform;
    WzGetBehaviorTransformFn get_world_transform;
    WzGetBehaviorPositionFn get_local_position;
    WzGetBehaviorPositionFn get_world_position;

    void* command_writer_user;
    WzWriteBehaviorCommandFn write_command;

    void* log_user;
    WzBehaviorLogFn log_info;

    void* collision_query_user;
    WzQueryBehaviorCollisionSurfaceRayFn query_collision_surface_ray;

    const WzFrameTiming* timing;
} WzBehaviorFrameFacts;

typedef void (*WzBehaviorFn)(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    void* user_data);

typedef void (*WzBehaviorModuleEventFn)(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    void* user_data);

typedef uint8_t (*WzRegisterBehaviorFn)(
    void* user,
    const char* module,
    const char* name,
    WzBehaviorFn function,
    void* behavior_user_data);

typedef uint8_t (*WzRegisterBehaviorModuleFn)(
    void* user,
    const char* module,
    WzBehaviorModuleEventFn on_event,
    void* module_user_data);

typedef struct WzBehaviorPluginApi
{
    uint32_t version;
    void* user;
    WzRegisterBehaviorFn register_behavior;
    WzRegisterBehaviorModuleFn register_module;
} WzBehaviorPluginApi;

typedef uint8_t (*WzRegisterBehaviorPluginFn)(
    WzBehaviorPluginApi* api);

// Dynamic modules should export this symbol with C linkage:
// uint8_t wz_register_behaviors(WzBehaviorPluginApi* api);

#ifdef __cplusplus
}
#endif
