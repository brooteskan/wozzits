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

#define WZ_BEHAVIOR_ABI_VERSION 24u
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
    /*
     * Make `entity` the runtime's active camera. Unlike the transform/velocity
     * commands above, this does not mutate the entity: the host reads the
     * entity's world transform + its SceneCameraAsset (fov/near/far) and points
     * the runtime camera at it. `values` are unused. Typically issued once by a
     * scene-setup behavior on WZ_EVENT_SCENE_LOADED; the host applies it after
     * the dispatch, before the frame loop. apply_behavior_commands ignores it
     * (it is handled at the WozzitsApp_v1 level, which owns the camera).
     */
    WZ_BEHAVIOR_COMMAND_SET_ACTIVE_CAMERA = 11u,
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

/*
 * Deferred runtime-authoring: request that a new child node be spawned under
 * `parent_entity` in the running scene. Unlike write_command (which buffers a
 * transform/velocity command applied later the same frame), this is a CHEAP
 * live scene-ECS authoring edit issued by a behavior. It is:
 *   - Deferred: the request is queued during behavior dispatch and applied at
 *     the next frame boundary, through the SAME runtime apply path the host's
 *     add_child uses. It does NOT mutate the scene mid-dispatch (the engine is
 *     iterating the scene to run behaviors).
 *   - Fire-and-forget: no id is returned (a behavior runs on the engine thread,
 *     so it cannot block on the host's cross-thread add-child handshake).
 *     Returning a reserved id is a documented follow-up.
 * Returns 1 if the request was accepted (queued), 0 otherwise (e.g. the parent
 * entity could not be resolved to an authored scene node). Behaviors may only
 * issue cheap live-ECS edits like this — never an asset-graph/behavior-binding
 * mutation that would trigger a recompile.
 */
typedef uint8_t (*WzSpawnChildFn)(
    void* user,
    WzBehaviorEntityId parent_entity);

/*
 * Deferred runtime-authoring: request that the node bound to `entity` be
 * removed from the running scene (it and its subtree). Same contract as
 * spawn_child — a CHEAP live scene-ECS authoring edit, deferred to the next
 * frame boundary, applied through the SAME runtime apply path the host's
 * remove uses, fire-and-forget. Returns 1 if the request was accepted
 * (queued; the entity resolved to an authored scene node), 0 otherwise.
 */
typedef uint8_t (*WzRemoveNodeFn)(
    void* user,
    WzBehaviorEntityId entity);

/*
 * Deferred runtime-authoring: set the preferred asset-graph renderable of the
 * node bound to `entity` to the authored asset-graph node `asset_graph_node_id`
 * (0 clears the renderable). Same contract as spawn_child — a CHEAP live
 * scene-ECS authoring edit (a field write + dirty flag; it does NOT recompile
 * the asset DAG), deferred to the next frame boundary, applied through the SAME
 * runtime apply path the host's set_node_renderable_asset uses, fire-and-forget.
 * Returns 1 if the request was accepted (queued; the entity resolved to an
 * authored scene node), 0 otherwise.
 */
typedef uint8_t (*WzSetRenderableAssetFn)(
    void* user,
    WzBehaviorEntityId entity,
    uint64_t asset_graph_node_id);

/*
 * Deferred runtime-authoring: reparent the node bound to `entity` under the
 * node bound to `new_parent_entity` in the running scene. Same contract as
 * spawn_child — a CHEAP live scene-ECS authoring edit, deferred to the next
 * frame boundary, applied through the SAME runtime apply path the host's
 * reparent uses (reparent_node), fire-and-forget.
 * `new_parent_entity == WZ_INVALID_BEHAVIOR_ENTITY` detaches the node to the
 * TOP LEVEL (no parent). `entity` itself must resolve to an authored scene
 * node. Returns 1 if the request was accepted (queued; `entity` resolved), 0
 * otherwise.
 */
typedef uint8_t (*WzReparentNodeFn)(
    void* user,
    WzBehaviorEntityId entity,
    WzBehaviorEntityId new_parent_entity);

/*
 * Deferred runtime-authoring: add an optional component of the given `kind`
 * token ("camera" | "proximity" | "collision" | "motion") to the node bound to
 * `entity`. Same contract as spawn_child — a CHEAP live scene-ECS authoring
 * edit, deferred to the next frame boundary, applied through the SAME runtime
 * apply path the host's add_node_component uses, fire-and-forget. The `kind`
 * string is read during the call only (it is copied into the queued request).
 * Returns 1 if the request was accepted (queued; the entity resolved to an
 * authored scene node), 0 otherwise.
 */
typedef uint8_t (*WzAddNodeComponentFn)(
    void* user,
    WzBehaviorEntityId entity,
    const char* kind);

/*
 * Deferred runtime-authoring: remove the optional component of the given
 * `kind` token ("camera" | "proximity" | "collision" | "motion") from the node
 * bound to `entity`. Same contract as add_node_component above — a CHEAP live
 * scene-ECS authoring edit, deferred to the next frame boundary, applied
 * through the SAME runtime apply path the host's remove_node_component uses,
 * fire-and-forget. The `kind` string is read during the call only (it is
 * copied into the queued request). Returns 1 if the request was accepted
 * (queued; the entity resolved to an authored scene node), 0 otherwise.
 */
typedef uint8_t (*WzRemoveNodeComponentFn)(
    void* user,
    WzBehaviorEntityId entity,
    const char* kind);

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
    /*
     * For input structured-buffer ports, the engine binds one component of
     * the entity mesh's compiled sparse operator asset (CSR; see
     * MeshSparseOperatorData). u32[0] selects the component
     * (WZ_GPU_SPARSE_OPERATOR_*), u32[1] selects the operator kind ordinal
     * (0 = uniform vertex Laplacian). All components are 4-byte elements
     * (uint offsets/indices, float weights/mass); element_count is filled
     * by the engine. The operator asset must already be compiled for the
     * entity's mesh, or the job fails with a clear reason. Buffers are
     * GPU-resident: uploaded once per operator and reused across
     * dispatches.
     *
     * Weight convention for NeighborWeights operators: rows store neighbor
     * weights only, so kernels apply
     *   smooth[i]   = sum_j w_ij * x[j]
     *   residual[i] = x[i] - smooth[i]
     * and rows with no neighbors (row_offsets[i] == row_offsets[i+1]) must
     * output residual 0 — an isolated vertex is not detail.
     */
    WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR = 6u,
    /*
     * For u32 root-constant ports, the engine fills up to four dwords of
     * operator metadata so the kernel can validate what it is consuming:
     *   u32[0] = row_count (domain element count)
     *   u32[1] = nonzero_count
     *   u32[2] = operator kind ordinal (MeshSparseOperatorKind)
     *   u32[3] = value convention ordinal
     *            (MeshSparseOperatorValueConvention)
     * u32[1] of the declared port selects the operator kind, matching the
     * buffer ref above.
     */
    WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR_INFO = 7u,
    /*
     * For input structured-buffer ports, the engine binds a Float1 channel
     * from the current entity's MeshDerivedField as a read-only signal
     * (StructuredBuffer<float>, stride 4). The field asset is resolved
     * through the entity's mesh field visualization target; u32[0] selects
     * the channel id. v0 requires:
     *   u32[1] = WZ_GPU_MESH_FIELD_VALUE_FLOAT1
     *   u32[2] = WZ_GPU_MESH_FIELD_COMPONENT_ALL or
     *            WZ_GPU_MESH_FIELD_COMPONENT_X
     *   u32[3] = WZ_GPU_SPARSE_APPLY_RESIDUAL
     *
     * The input field domain must match the sparse operator domain. v0 is
     * vertex-only: operator domain = Vertex, input field domain = Vertex,
     * output field domain = Vertex. Missing channel, domain mismatch, type
     * mismatch, missing operator, and row/element-count mismatch fail the
     * job with a clear reason.
     */
    WZ_GPU_RESOURCE_REF_MESH_DERIVED_FIELD_CHANNEL = 8u,
};

/* Component selector for WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR ports. */
enum
{
    WZ_GPU_SPARSE_OPERATOR_ROW_OFFSETS = 0u,  /* uint, row_count + 1 */
    WZ_GPU_SPARSE_OPERATOR_COL_INDICES = 1u,  /* uint, nonzero_count */
    WZ_GPU_SPARSE_OPERATOR_WEIGHTS = 2u,      /* float, nonzero_count */
    WZ_GPU_SPARSE_OPERATOR_VERTEX_MASS = 3u,  /* float, row_count */
};

/* Value type selector for WZ_GPU_RESOURCE_REF_MESH_DERIVED_FIELD_CHANNEL. */
enum
{
    WZ_GPU_MESH_FIELD_VALUE_FLOAT1 = 0u,
    WZ_GPU_MESH_FIELD_VALUE_FLOAT2 = 1u,
    WZ_GPU_MESH_FIELD_VALUE_FLOAT3 = 2u,
    WZ_GPU_MESH_FIELD_VALUE_FLOAT4 = 3u,
    WZ_GPU_MESH_FIELD_VALUE_UINT1 = 4u,
};

/* Component selector for vector-capable mesh field signal descriptors. */
enum
{
    WZ_GPU_MESH_FIELD_COMPONENT_ALL = 0u,
    WZ_GPU_MESH_FIELD_COMPONENT_X = 1u,
    WZ_GPU_MESH_FIELD_COMPONENT_Y = 2u,
    WZ_GPU_MESH_FIELD_COMPONENT_Z = 3u,
    WZ_GPU_MESH_FIELD_COMPONENT_W = 4u,
    WZ_GPU_MESH_FIELD_COMPONENT_MAGNITUDE = 5u,
};

/* Sparse-operator apply mode for mesh field signal descriptors. */
enum
{
    WZ_GPU_SPARSE_APPLY_RESIDUAL = 0u,
    WZ_GPU_SPARSE_APPLY_SMOOTH = 1u,
    WZ_GPU_SPARSE_APPLY_MAGNITUDE_RESIDUAL = 2u,
    WZ_GPU_SPARSE_APPLY_DIFFUSION_STEP = 3u,
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
 * AUTO is the legacy derivation: the largest element count the engine
 * resolved for vertex-sized ports (vertex positions input, mesh field
 * visualization output, vertex count constant). Topology ports (mesh
 * indices input, triangle count constant) deliberately do not feed AUTO —
 * reading topology must not inflate a vertex-domain dispatch. A kernel
 * that iterates topology declares FACE or CORNER explicitly; AUTO with
 * nothing resolved fails the job with a clear reason.
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

    /*
     * Deferred runtime-authoring (#204). Appended at the end so existing
     * behavior DLLs stay binary-compatible (WzBehaviorFrameFacts is
     * pointer-passed and not version-checked). spawn_child queues a child-add
     * applied at the next frame boundary via the shared WozzitsApp_v1 apply
     * path; null when the runtime exposes no deferred-authoring sink.
     *
     * remove_node and set_renderable_asset (appended after spawn_child; same
     * APPEND-ONLY rule) share deferred_authoring_user and are queued + drained
     * the same way through their matching WozzitsApp_v1 apply methods.
     *
     * reparent_node, add_node_component and remove_node_component (appended
     * after set_renderable_asset; same APPEND-ONLY rule) likewise share
     * deferred_authoring_user and route to reparent_node / add_node_component /
     * remove_node_component on WozzitsApp_v1.
     */
    void* deferred_authoring_user;
    WzSpawnChildFn spawn_child;
    WzRemoveNodeFn remove_node;
    WzSetRenderableAssetFn set_renderable_asset;
    WzReparentNodeFn reparent_node;
    WzAddNodeComponentFn add_node_component;
    WzRemoveNodeComponentFn remove_node_component;
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
