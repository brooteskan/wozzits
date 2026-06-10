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

#define WZ_BEHAVIOR_ABI_VERSION 22u
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
    WZ_EVENT_INPUT_KEY_PRESSED = 300u,
    WZ_EVENT_INPUT_KEY_RELEASED = 301u,
    WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED = 302u,
    WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED = 303u,
    WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED = 304u,
    WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED = 305u,
    WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED = 306u,
    WZ_EVENT_GPU_COMPUTE_REQUEST = 399u,
    WZ_EVENT_GPU_COMPUTE_COMPLETED = 400u,
    WZ_EVENT_GPU_COMPUTE_FAILED = 401u,
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

#define WZ_INPUT_EVENT_INVALID_VALUE UINT32_C(0xffffffff)

typedef struct WzInputEventPayload
{
    uint32_t key;
    uint32_t controller;
    uint32_t button;
    uint32_t axis;
    float value;
} WzInputEventPayload;

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

typedef struct WzGpuWorkId
{
    uint64_t value;
} WzGpuWorkId;

typedef uint32_t WzGpuPortKind;
enum
{
    WZ_GPU_PORT_NONE = 0u,
    WZ_GPU_PORT_STRUCTURED_BUFFER = 1u,
    WZ_GPU_PORT_U32 = 2u,
    WZ_GPU_PORT_F32 = 3u,
};

typedef uint32_t WzGpuPortDirection;
enum
{
    WZ_GPU_PORT_INPUT = 1u,
    WZ_GPU_PORT_OUTPUT = 2u,
    WZ_GPU_PORT_INPUT_OUTPUT = 3u,
};

typedef struct WzGpuResourceRef
{
    uint64_t value;
} WzGpuResourceRef;

enum
{
    WZ_GPU_RESOURCE_REF_NONE = 0u,
    /*
     * For output structured-buffer ports, publish the resulting GPU buffer as
     * the current entity's mesh field visualization channel. u32[0] may hold
     * the channel id; 0 means use the entity's authored mesh render style
     * visualization channel. element_count 0 means the engine fills it from
     * the target field's element count.
     */
    WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION = 1u,
    /*
     * For input structured-buffer ports, the engine binds the current
     * entity's mesh vertex positions (one float3 per vertex, stride 12).
     * No initial_data is required; element_count is filled by the engine
     * from the mesh vertex count. Requires the entity to have a mesh field
     * visualization target (the mesh is resolved through it).
     */
    WZ_GPU_RESOURCE_REF_MESH_VERTEX_POSITIONS = 2u,
    /*
     * For u32 root-constant ports, the engine fills the value with the
     * current entity's mesh vertex count.
     */
    WZ_GPU_RESOURCE_REF_MESH_VERTEX_COUNT = 3u,
    /*
     * For input structured-buffer ports, the engine binds the current
     * entity's mesh index buffer (StructuredBuffer<uint>, stride 4,
     * triangle list). No initial_data is required; element_count is filled
     * by the engine from the mesh index count. Requires the entity to have
     * a mesh field visualization target (the mesh is resolved through it).
     */
    WZ_GPU_RESOURCE_REF_MESH_INDICES = 4u,
    /*
     * For u32 root-constant ports, the engine fills the value with the
     * current entity's mesh triangle count (index count / 3).
     */
    WZ_GPU_RESOURCE_REF_MESH_TRIANGLE_COUNT = 5u,
};

typedef struct WzGpuPortValue
{
    const char* name;
    WzGpuPortKind kind;
    WzGpuPortDirection direction;
    uint32_t element_count;
    uint32_t stride_bytes;
    const void* initial_data;
    uint64_t initial_data_bytes;
    WzGpuResourceRef resource;
    uint32_t u32[4];
    float f32[4];
} WzGpuPortValue;

/*
 * Logical iteration domain of a compute job. When group_count_x is 0 the
 * engine derives the dispatch group count from the domain's element count
 * and the kernel's authored thread group size. The domain names align with
 * the engine's MeshDerivedFieldDomain vocabulary (FACE = triangle,
 * CORNER = index in the triangle list).
 *
 * AUTO preserves the legacy derivation: the largest element count the
 * engine resolved for any mesh-bound port. Note that AUTO conflates the
 * data a kernel reads with the domain it iterates — a vertex-domain kernel
 * that merely reads indices over-dispatches by roughly index count.
 * Declare an explicit domain for any kernel that mixes domains.
 */
typedef uint32_t WzGpuDispatchDomain;

enum
{
    WZ_GPU_DISPATCH_DOMAIN_AUTO = 0u,
    /* Mesh vertex count. */
    WZ_GPU_DISPATCH_DOMAIN_VERTEX = 1u,
    /*
     * Reserved: unique mesh edge count. Not resolvable until resident mesh
     * topology (the resident mesh-data table) provides it; declaring it
     * fails the job with a clear reason.
     */
    WZ_GPU_DISPATCH_DOMAIN_EDGE = 2u,
    /* Mesh triangle count (index count / 3). */
    WZ_GPU_DISPATCH_DOMAIN_FACE = 3u,
    /* Mesh index count (one element per triangle-list corner). */
    WZ_GPU_DISPATCH_DOMAIN_CORNER = 4u,
    /* Element count of the job's first output port. */
    WZ_GPU_DISPATCH_DOMAIN_OUTPUT = 5u,
};

typedef struct WzGpuComputeJobDesc
{
    const char* kernel;
    const WzGpuPortValue* ports;
    uint32_t port_count;
    uint32_t group_count_x;
    uint32_t group_count_y;
    uint32_t group_count_z;
    WzGpuDispatchDomain dispatch_domain;
    uint64_t request_tag;
} WzGpuComputeJobDesc;

typedef struct WzGpuKernelPortContractDesc
{
    const char* name;
    WzGpuPortKind kind;
    WzGpuPortDirection direction;
    uint32_t stride_bytes;
} WzGpuKernelPortContractDesc;

typedef struct WzGpuKernelContractDesc
{
    const char* kernel_id;
    const WzGpuKernelPortContractDesc* ports;
    uint32_t port_count;
} WzGpuKernelContractDesc;

typedef uint8_t (*WzSubmitGpuComputeJobFn)(
    void* user,
    const WzGpuComputeJobDesc* job,
    WzGpuWorkId* out_work);

typedef uint32_t WzGpuComputeStatus;
enum
{
    WZ_GPU_COMPUTE_STATUS_NONE = 0u,
    WZ_GPU_COMPUTE_STATUS_COMPLETED = 1u,
    WZ_GPU_COMPUTE_STATUS_FAILED = 2u,
};

typedef struct WzGpuComputeOutputView
{
    const char* name;
    WzGpuPortKind kind;
    uint32_t element_count;
    uint32_t stride_bytes;
    const void* bytes;
    uint64_t byte_count;
} WzGpuComputeOutputView;

typedef struct WzGpuComputeEventPayload
{
    WzGpuWorkId work;
    WzGpuComputeStatus status;
    uint64_t request_tag;
    uint32_t output_count;
    const WzGpuComputeOutputView* outputs;
} WzGpuComputeEventPayload;

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

typedef uint8_t (*WzSampleTerrainSurfaceFn)(
    void* user,
    WzBehaviorEntityId terrain_entity,
    float world_x,
    float world_z,
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

typedef void* (*WzAllocBehaviorStateFn)(
    void* user,
    uint32_t size,
    uint32_t alignment);

typedef struct WzBehaviorStateDesc
{
    uint32_t size;
    uint32_t alignment;
    uint32_t layout_version;
} WzBehaviorStateDesc;

typedef void* (*WzAllocBehaviorStateDescFn)(
    void* user,
    const WzBehaviorStateDesc* desc);

typedef void* (*WzGetBehaviorStateFn)(void* user);

typedef void* (*WzCreateSharedBehaviorStateFn)(
    void* user,
    const char* key,
    uint32_t size,
    uint32_t alignment);

typedef void* (*WzCreateSharedBehaviorStateDescFn)(
    void* user,
    const char* key,
    const WzBehaviorStateDesc* desc);

typedef void* (*WzFindSharedBehaviorStateFn)(
    void* user,
    const char* key);

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
    WzSampleTerrainSurfaceFn sample_terrain_surface;

    const WzFrameTiming* timing;

    void* scene_query_user;
    WzFindBehaviorEntityByNameFn find_entity_by_name;
    WzFindBehaviorEntityByAuthoredIdFn find_entity_by_authored_id;

    void* behavior_config_user;
    WzGetBehaviorConfigBoolFn get_config_bool;
    WzGetBehaviorConfigNumberFn get_config_number;
    WzGetBehaviorConfigStringFn get_config_string;

    const WzInputEventPayload* active_input_event;
    const WzGpuComputeEventPayload* active_gpu_compute_event;

    void* behavior_state_user;
    WzGetBehaviorStateFn get_instance_state;
    WzFindSharedBehaviorStateFn find_shared_state;

    void* gpu_compute_user;
    WzSubmitGpuComputeJobFn submit_gpu_compute;
} WzBehaviorFrameFacts;

typedef struct WzBehaviorInitFacts
{
    void* transform_query_user;
    WzGetBehaviorTransformFn get_local_transform;
    WzGetBehaviorTransformFn get_world_transform;
    WzGetBehaviorPositionFn get_local_position;
    WzGetBehaviorPositionFn get_world_position;

    void* log_user;
    WzBehaviorLogFn log_info;

    void* scene_query_user;
    WzFindBehaviorEntityByNameFn find_entity_by_name;
    WzFindBehaviorEntityByAuthoredIdFn find_entity_by_authored_id;

    void* behavior_config_user;
    WzGetBehaviorConfigBoolFn get_config_bool;
    WzGetBehaviorConfigNumberFn get_config_number;
    WzGetBehaviorConfigStringFn get_config_string;

    void* behavior_state_user;
    WzAllocBehaviorStateFn alloc_instance_state;
    WzGetBehaviorStateFn get_instance_state;
    WzCreateSharedBehaviorStateFn create_shared_state;
    WzFindSharedBehaviorStateFn find_shared_state;
    WzAllocBehaviorStateDescFn alloc_instance_state_desc;
    WzCreateSharedBehaviorStateDescFn create_shared_state_desc;
} WzBehaviorInitFacts;

typedef void (*WzBehaviorFn)(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    void* user_data);

typedef void (*WzBehaviorModuleEventFn)(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    void* user_data);

typedef void (*WzBehaviorInitFn)(
    const WzBehaviorInitFacts* facts,
    WzBehaviorEntityId entity,
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

typedef struct WzBehaviorModuleDesc
{
    uint32_t size;
    const char* module;
    WzBehaviorModuleEventFn on_event;
    WzBehaviorInitFn on_init;
    const char* const* event_channels;
    uint32_t event_channel_count;
    void* module_user_data;
} WzBehaviorModuleDesc;

typedef uint8_t (*WzRegisterBehaviorModuleDescFn)(
    void* user,
    const WzBehaviorModuleDesc* desc);

typedef uint8_t (*WzRegisterGpuKernelContractFn)(
    void* user,
    const WzGpuKernelContractDesc* desc);

typedef struct WzBehaviorPluginApi
{
    uint32_t version;
    void* user;
    WzRegisterBehaviorFn register_behavior;
    WzRegisterBehaviorModuleFn register_module;
    WzRegisterBehaviorModuleDescFn register_module_desc;
    WzRegisterGpuKernelContractFn register_gpu_kernel_contract;
} WzBehaviorPluginApi;

typedef uint8_t (*WzRegisterBehaviorPluginFn)(
    WzBehaviorPluginApi* api);

// Dynamic modules should export this symbol with C linkage:
// uint8_t wz_register_behaviors(WzBehaviorPluginApi* api);

#ifdef __cplusplus
}
#endif
