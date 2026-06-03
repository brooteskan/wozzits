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

#define WZ_BEHAVIOR_ABI_VERSION 6u
#define WZ_BEHAVIOR_PLUGIN_REGISTER_SYMBOL "wz_register_behaviors"

#define WZ_MAX_CONTROLLERS 4u
#define WZ_CONTROLLER_AXIS_COUNT 8u
#define WZ_CONTROLLER_BUTTON_COUNT 16u

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

    uint8_t controller_count;
    uint8_t controller_connected[WZ_MAX_CONTROLLERS];
    uint8_t controller_connected_pressed[WZ_MAX_CONTROLLERS];
    uint8_t controller_connected_released[WZ_MAX_CONTROLLERS];
    float controller_axes[WZ_MAX_CONTROLLERS][WZ_CONTROLLER_AXIS_COUNT];
    uint8_t controller_buttons[WZ_MAX_CONTROLLERS][WZ_CONTROLLER_BUTTON_COUNT];
    uint8_t controller_buttons_pressed
        [WZ_MAX_CONTROLLERS][WZ_CONTROLLER_BUTTON_COUNT];
    uint8_t controller_buttons_released
        [WZ_MAX_CONTROLLERS][WZ_CONTROLLER_BUTTON_COUNT];
} WzInputStateView;

/* Keyboard indices match Windows virtual-key codes for now. */
enum
{
    WZ_KEY_A = 65u,
    WZ_KEY_D = 68u,
    WZ_KEY_S = 83u,
    WZ_KEY_W = 87u,
    WZ_KEY_SPACE = 32u,
    WZ_KEY_ESCAPE = 27u,
    WZ_KEY_SHIFT = 16u,
    WZ_KEY_CONTROL = 17u,
};

enum
{
    WZ_MOUSE_BUTTON_LEFT = 0u,
    WZ_MOUSE_BUTTON_RIGHT = 1u,
    WZ_MOUSE_BUTTON_MIDDLE = 2u,
};

enum
{
    WZ_CONTROLLER_AXIS_LEFT_X = 0u,
    WZ_CONTROLLER_AXIS_LEFT_Y = 1u,
    WZ_CONTROLLER_AXIS_RIGHT_X = 2u,
    WZ_CONTROLLER_AXIS_RIGHT_Y = 3u,
    WZ_CONTROLLER_AXIS_LEFT_TRIGGER = 4u,
    WZ_CONTROLLER_AXIS_RIGHT_TRIGGER = 5u,
};

enum
{
    WZ_CONTROLLER_BUTTON_DPAD_UP = 0u,
    WZ_CONTROLLER_BUTTON_DPAD_DOWN = 1u,
    WZ_CONTROLLER_BUTTON_DPAD_LEFT = 2u,
    WZ_CONTROLLER_BUTTON_DPAD_RIGHT = 3u,
    WZ_CONTROLLER_BUTTON_START = 4u,
    WZ_CONTROLLER_BUTTON_BACK = 5u,
    WZ_CONTROLLER_BUTTON_LEFT_THUMB = 6u,
    WZ_CONTROLLER_BUTTON_RIGHT_THUMB = 7u,
    WZ_CONTROLLER_BUTTON_LEFT_SHOULDER = 8u,
    WZ_CONTROLLER_BUTTON_RIGHT_SHOULDER = 9u,
    WZ_CONTROLLER_BUTTON_A = 10u,
    WZ_CONTROLLER_BUTTON_B = 11u,
    WZ_CONTROLLER_BUTTON_X = 12u,
    WZ_CONTROLLER_BUTTON_Y = 13u,
};

typedef uint32_t WzBehaviorCommandKind;
typedef uint32_t WzBehaviorMotionSpace;
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
    WZ_BEHAVIOR_COMMAND_SET_ANGULAR_VELOCITY = 9u,
    WZ_BEHAVIOR_COMMAND_SET_MOTION_SPACE = 10u,
};

enum
{
    WZ_BEHAVIOR_MOTION_SPACE_WORLD = 0u,
    WZ_BEHAVIOR_MOTION_SPACE_LOCAL = 1u,
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

typedef uint8_t (*WzFindBehaviorEntityByNameFn)(
    void* user,
    const char* name,
    WzBehaviorEntityId* out_entity);

typedef uint8_t (*WzFindBehaviorEntityByAuthoredIdFn)(
    void* user,
    const char* authored_id,
    WzBehaviorEntityId* out_entity);

typedef uint8_t (*WzGetBehaviorConfigBoolFn)(
    void* user,
    const char* key,
    uint8_t* out_value);

typedef uint8_t (*WzGetBehaviorConfigNumberFn)(
    void* user,
    const char* key,
    double* out_value);

typedef uint8_t (*WzGetBehaviorConfigStringFn)(
    void* user,
    const char* key,
    char* out_buffer,
    uint32_t buffer_size,
    uint32_t* out_required_size);

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

    void* scene_query_user;
    WzFindBehaviorEntityByNameFn find_entity_by_name;
    WzFindBehaviorEntityByAuthoredIdFn find_entity_by_authored_id;

    void* behavior_config_user;
    WzGetBehaviorConfigBoolFn get_config_bool;
    WzGetBehaviorConfigNumberFn get_config_number;
    WzGetBehaviorConfigStringFn get_config_string;
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
