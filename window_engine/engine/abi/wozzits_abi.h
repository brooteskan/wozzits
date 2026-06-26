#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WZ_ABI_VERSION 23u

#if defined(_WIN32) && defined(WZ_ABI_EXPORTS)
#define WZ_ABI_API __declspec(dllexport)
#elif !defined(_WIN32) && defined(WZ_ABI_EXPORTS)
#define WZ_ABI_API __attribute__((visibility("default")))
#else
#define WZ_ABI_API
#endif

typedef uint32_t WzResultCode;
enum
{
    WZ_RESULT_OK = 0u,
    WZ_RESULT_INVALID_ARGUMENT = 1u,
    WZ_RESULT_INTERNAL_ERROR = 2u,
    WZ_RESULT_OUT_OF_MEMORY = 3u,
};

typedef struct WzResult
{
    WzResultCode code;
    const char* message;
} WzResult;

typedef struct WzBuffer
{
    uint8_t* data;
    uint64_t size;
} WzBuffer;

typedef struct WzHostSession WzHostSession;

// Opaque handle to a running in-process engine runtime (Option Y, #189): the
// editor owns one engine instance that renders the project's viewport in its own
// window on an engine-owned thread. Started/stopped by the editor; no IPC.
typedef struct WzHostRuntime WzHostRuntime;

typedef void (*WzHostLogCallback)(
    uint32_t level,
    const char* timestamp_utf8,
    uint64_t timestamp_size,
    const char* message_utf8,
    uint64_t message_size,
    void* user);

typedef uint32_t WzEditorProjectStatus;
enum
{
    WZ_EDITOR_PROJECT_STATUS_MISSING = 0u,
    WZ_EDITOR_PROJECT_STATUS_INVALID = 1u,
    WZ_EDITOR_PROJECT_STATUS_VALID = 2u,
};

typedef struct WzEditorStringSpan
{
    uint64_t offset;
    uint64_t size;
} WzEditorStringSpan;

typedef struct WzEditorTableSpan
{
    uint64_t offset;
    uint64_t count;
} WzEditorTableSpan;

typedef struct WzEditorAssetGraphPort
{
    uint32_t index;
    uint32_t type;
    uint32_t flags;
    uint32_t reserved;
    WzEditorStringSpan name;
    WzEditorStringSpan label;
    WzEditorStringSpan type_name;
} WzEditorAssetGraphPort;

typedef struct WzEditorAssetGraphDiagnostic
{
    uint32_t severity;
    uint32_t code;
    uint64_t node;
    uint64_t edge;
    uint32_t input_port;
    uint32_t reserved;
    WzEditorStringSpan message;
    WzEditorStringSpan severity_name;
    WzEditorStringSpan code_name;
} WzEditorAssetGraphDiagnostic;

typedef struct WzEditorAssetGraphParam
{
    WzEditorStringSpan name;
    // Declared widget type: "bool" | "int" | "float" | "float3" | "color" |
    // "string" | "filepath" | "enum".
    WzEditorStringSpan kind;
    WzEditorStringSpan value;  // display text; for "enum", the selected option
    WzEditorTableSpan options; // enum choices (WzEditorStringSpan[]; "enum" only)
} WzEditorAssetGraphParam;

typedef uint32_t WzEditorAssetGraphPortFlags;
enum
{
    WZ_EDITOR_ASSET_GRAPH_PORT_REQUIRED = 1u << 0u,
    WZ_EDITOR_ASSET_GRAPH_PORT_MANY = 1u << 1u,
};

typedef struct WzEditorAssetGraphNode
{
    uint64_t id;
    uint32_t type;
    uint32_t reserved;
    WzEditorStringSpan type_name;
    WzEditorStringSpan schema;
    WzEditorStringSpan display_name;
    WzEditorStringSpan compile_status;
    double x;
    double y;
    WzEditorTableSpan input_ports;
    WzEditorTableSpan output_ports;
    WzEditorTableSpan diagnostics;
    WzEditorTableSpan params;
} WzEditorAssetGraphNode;

typedef struct WzEditorAssetGraphEdge
{
    uint64_t id;
    uint64_t from;
    uint64_t to;
    uint32_t to_input_port;
    uint32_t reserved;
} WzEditorAssetGraphEdge;

typedef uint32_t WzEditorAssetGraphConnectionStatus;
enum
{
    WZ_EDITOR_ASSET_GRAPH_CONNECTION_COMPATIBLE = 0u,
    WZ_EDITOR_ASSET_GRAPH_CONNECTION_MISSING_NODE = 1u,
    WZ_EDITOR_ASSET_GRAPH_CONNECTION_MISSING_COMPILER = 2u,
    WZ_EDITOR_ASSET_GRAPH_CONNECTION_INVALID_INPUT_PORT = 3u,
    WZ_EDITOR_ASSET_GRAPH_CONNECTION_TYPE_MISMATCH = 4u,
    WZ_EDITOR_ASSET_GRAPH_CONNECTION_SELF_DEPENDENCY = 5u,
    WZ_EDITOR_ASSET_GRAPH_CONNECTION_CYCLE = 6u,
    WZ_EDITOR_ASSET_GRAPH_CONNECTION_DUPLICATE_INPUT_PORT = 7u,
};

typedef struct WzEditorAssetGraphConnectionCheck
{
    uint32_t abi_version;
    uint32_t compatible;
    WzEditorAssetGraphConnectionStatus status;
    uint32_t replaces_existing;
    uint64_t from;
    uint64_t to;
    uint32_t to_input_port;
    uint32_t reserved;
    uint32_t from_type;
    uint32_t to_type;
    WzEditorStringSpan message;
    WzEditorStringSpan from_type_name;
    WzEditorStringSpan to_type_name;
} WzEditorAssetGraphConnectionCheck;

typedef struct WzEditorAssetGraphSnapshot
{
    uint32_t ok;
    uint32_t reserved;
    WzEditorStringSpan error;
    WzEditorStringSpan schema;
    WzEditorTableSpan nodes;
    WzEditorTableSpan edges;
    double zoom;
} WzEditorAssetGraphSnapshot;

typedef uint32_t WzEditorSceneNodeFlags;
enum
{
    WZ_EDITOR_SCENE_NODE_HAS_PARENT = 1u << 0u,
    WZ_EDITOR_SCENE_NODE_HAS_VISIBLE = 1u << 1u,
    WZ_EDITOR_SCENE_NODE_VISIBLE = 1u << 2u,
    WZ_EDITOR_SCENE_NODE_HAS_TRANSFORM = 1u << 3u,
    WZ_EDITOR_SCENE_NODE_HAS_CAMERA = 1u << 4u,
    WZ_EDITOR_SCENE_NODE_HAS_RENDERABLE = 1u << 5u,
    WZ_EDITOR_SCENE_NODE_RENDERABLE_HAS_ASSET_GRAPH_NODE_ID = 1u << 6u,
    WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE = 1u << 7u,
};

typedef uint32_t WzEditorSceneCameraFlags;
enum
{
    WZ_EDITOR_SCENE_CAMERA_HAS_FOV_Y = 1u << 0u,
    WZ_EDITOR_SCENE_CAMERA_HAS_NEAR_PLANE = 1u << 1u,
    WZ_EDITOR_SCENE_CAMERA_HAS_FAR_PLANE = 1u << 2u,
    WZ_EDITOR_SCENE_CAMERA_HAS_ASPECT = 1u << 3u,
};

typedef struct WzEditorSceneTransform
{
    double translation_x;
    double translation_y;
    double translation_z;
    double rotation_quaternion_x;
    double rotation_quaternion_y;
    double rotation_quaternion_z;
    double rotation_quaternion_w;
    double rotation_euler_degrees_x;
    double rotation_euler_degrees_y;
    double rotation_euler_degrees_z;
    double scale_x;
    double scale_y;
    double scale_z;
    WzEditorStringSpan display_translation_x;
    WzEditorStringSpan display_translation_y;
    WzEditorStringSpan display_translation_z;
    WzEditorStringSpan display_rotation_x;
    WzEditorStringSpan display_rotation_y;
    WzEditorStringSpan display_rotation_z;
    WzEditorStringSpan display_scale_x;
    WzEditorStringSpan display_scale_y;
    WzEditorStringSpan display_scale_z;
} WzEditorSceneTransform;

typedef struct WzEditorSceneCamera
{
    double fov_y;
    double near_plane;
    double far_plane;
    double aspect;
    WzEditorSceneCameraFlags flags;
    uint32_t reserved;
} WzEditorSceneCamera;

typedef struct WzEditorSceneRenderable
{
    uint64_t asset_graph_node_id;
} WzEditorSceneRenderable;

typedef struct WzEditorSceneRenderableSource
{
    WzEditorStringSpan kind;
    WzEditorStringSpan display_name;
} WzEditorSceneRenderableSource;

// Read-only summary of a node's GLB scene-source descriptor (issue #213),
// present only when WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE is set on the node.
// The full MeshRenderStyleData (base_style / per-mesh overrides) is not packed:
// has_base_style is 0/1 and style_override_count is the override array size.
typedef struct WzEditorSceneSceneSource
{
    WzEditorStringSpan kind;          // "glb"
    WzEditorStringSpan path;
    WzEditorStringSpan consume_mode;  // "instance" | "flatten"
    uint32_t scene_index;
    uint32_t style_override_count;
    uint32_t has_base_style;          // 0/1
    uint32_t reserved;
} WzEditorSceneSceneSource;

typedef struct WzEditorSceneComponent
{
    WzEditorStringSpan kind;
    WzEditorStringSpan display_name;
} WzEditorSceneComponent;

// A scene node's authored behavior binding (SceneBehaviorAsset). Distinct from
// the generic `components` table: behaviors carry full structured authoring data
// so the editor can render/inspect them. `enabled` is 0/1. `events` is a table
// of WzEditorStringSpan channel tokens (e.g. "frame.update"). `config` reuses
// the WzEditorAssetGraphParam shape (name + kind "bool"/"int"/"float"/"string"
// + display value) so the .NET side renders config uniformly with graph params.
typedef struct WzEditorSceneBehavior
{
    WzEditorStringSpan id;
    WzEditorStringSpan label;
    WzEditorStringSpan module;
    WzEditorStringSpan name;
    uint32_t enabled;
    uint32_t reserved;
    WzEditorTableSpan events; // WzEditorStringSpan[]
    WzEditorTableSpan config; // WzEditorAssetGraphParam[]
} WzEditorSceneBehavior;

typedef struct WzEditorSceneNode
{
    WzEditorStringSpan id;
    WzEditorStringSpan display_name;
    WzEditorStringSpan parent_id;
    WzEditorStringSpan kind;
    WzEditorSceneNodeFlags flags;
    uint32_t reserved;
    WzEditorSceneTransform transform;
    WzEditorSceneCamera camera;
    WzEditorSceneRenderable renderable;
    WzEditorSceneRenderableSource renderable_source;
    WzEditorTableSpan components;
    WzEditorTableSpan behaviors; // WzEditorSceneBehavior[]
    WzEditorTableSpan children;
    WzEditorSceneSceneSource scene_source;
} WzEditorSceneNode;

typedef struct WzEditorSceneSnapshot
{
    uint32_t ok;
    uint32_t reserved;
    WzEditorStringSpan error;
    WzEditorStringSpan schema;
    WzEditorStringSpan name;
    WzEditorTableSpan roots;
} WzEditorSceneSnapshot;

typedef struct WzEditorProjectSnapshot
{
    uint32_t abi_version;
    uint32_t ok;
    WzEditorProjectStatus status;
    uint32_t reserved;
    WzEditorStringSpan error;
    WzEditorStringSpan project_name;
    WzEditorAssetGraphSnapshot asset_graph;
    WzEditorSceneSnapshot scene;
} WzEditorProjectSnapshot;

typedef struct WzEditorProjectCreate
{
    uint32_t abi_version;
    uint32_t ok;
    WzEditorProjectStatus status;
    uint32_t created;
    WzEditorStringSpan error;
} WzEditorProjectCreate;

// ─── Asset catalog (authoring palette) ──────────────────────────────────────
// Device-free list of asset-graph node types the editor can author, grouped by
// output type with the schemas that produce each. Excludes types whose GPU
// residency has not yet moved to wozzits-rhi (see #186).

typedef struct WzEditorAssetCatalogSchema
{
    uint64_t schema;          // SchemaID.value
    WzEditorStringSpan label; // display label
} WzEditorAssetCatalogSchema;

typedef struct WzEditorAssetCatalogEntry
{
    uint32_t type;                  // AssetType value
    uint32_t reserved;
    WzEditorStringSpan type_name;
    WzEditorStringSpan category;
    WzEditorTableSpan schemas;      // WzEditorAssetCatalogSchema[]
} WzEditorAssetCatalogEntry;

typedef struct WzEditorAssetCatalog
{
    uint32_t abi_version;
    uint32_t ok;
    WzEditorTableSpan entries;      // WzEditorAssetCatalogEntry[]
} WzEditorAssetCatalog;

#ifdef __cplusplus
static_assert(sizeof(WzEditorStringSpan) == 16);
static_assert(offsetof(WzEditorStringSpan, offset) == 0);
static_assert(offsetof(WzEditorStringSpan, size) == 8);

static_assert(sizeof(WzEditorTableSpan) == 16);
static_assert(offsetof(WzEditorTableSpan, offset) == 0);
static_assert(offsetof(WzEditorTableSpan, count) == 8);

static_assert(sizeof(WzEditorAssetGraphPort) == 64);
static_assert(offsetof(WzEditorAssetGraphPort, index) == 0);
static_assert(offsetof(WzEditorAssetGraphPort, type) == 4);
static_assert(offsetof(WzEditorAssetGraphPort, flags) == 8);
static_assert(offsetof(WzEditorAssetGraphPort, name) == 16);
static_assert(offsetof(WzEditorAssetGraphPort, label) == 32);
static_assert(offsetof(WzEditorAssetGraphPort, type_name) == 48);

static_assert(sizeof(WzEditorAssetGraphDiagnostic) == 80);
static_assert(offsetof(WzEditorAssetGraphDiagnostic, severity) == 0);
static_assert(offsetof(WzEditorAssetGraphDiagnostic, code) == 4);
static_assert(offsetof(WzEditorAssetGraphDiagnostic, node) == 8);
static_assert(offsetof(WzEditorAssetGraphDiagnostic, input_port) == 24);
static_assert(offsetof(WzEditorAssetGraphDiagnostic, message) == 32);
static_assert(offsetof(WzEditorAssetGraphDiagnostic, severity_name) == 48);
static_assert(offsetof(WzEditorAssetGraphDiagnostic, code_name) == 64);

static_assert(sizeof(WzEditorAssetGraphParam) == 64);
static_assert(offsetof(WzEditorAssetGraphParam, name) == 0);
static_assert(offsetof(WzEditorAssetGraphParam, kind) == 16);
static_assert(offsetof(WzEditorAssetGraphParam, value) == 32);
static_assert(offsetof(WzEditorAssetGraphParam, options) == 48);

static_assert(sizeof(WzEditorAssetGraphNode) == 160);
static_assert(offsetof(WzEditorAssetGraphNode, id) == 0);
static_assert(offsetof(WzEditorAssetGraphNode, type) == 8);
static_assert(offsetof(WzEditorAssetGraphNode, type_name) == 16);
static_assert(offsetof(WzEditorAssetGraphNode, x) == 80);
static_assert(offsetof(WzEditorAssetGraphNode, input_ports) == 96);
static_assert(offsetof(WzEditorAssetGraphNode, output_ports) == 112);
static_assert(offsetof(WzEditorAssetGraphNode, diagnostics) == 128);
static_assert(offsetof(WzEditorAssetGraphNode, params) == 144);

static_assert(sizeof(WzEditorAssetGraphEdge) == 32);
static_assert(offsetof(WzEditorAssetGraphEdge, id) == 0);
static_assert(offsetof(WzEditorAssetGraphEdge, from) == 8);
static_assert(offsetof(WzEditorAssetGraphEdge, to) == 16);
static_assert(offsetof(WzEditorAssetGraphEdge, to_input_port) == 24);

static_assert(sizeof(WzEditorAssetGraphConnectionCheck) == 96);
static_assert(offsetof(WzEditorAssetGraphConnectionCheck, abi_version) == 0);
static_assert(offsetof(WzEditorAssetGraphConnectionCheck, from) == 16);
static_assert(offsetof(WzEditorAssetGraphConnectionCheck, to_input_port) == 32);
static_assert(offsetof(WzEditorAssetGraphConnectionCheck, from_type) == 40);
static_assert(offsetof(WzEditorAssetGraphConnectionCheck, message) == 48);

static_assert(sizeof(WzEditorAssetGraphSnapshot) == 80);
static_assert(offsetof(WzEditorAssetGraphSnapshot, ok) == 0);
static_assert(offsetof(WzEditorAssetGraphSnapshot, error) == 8);
static_assert(offsetof(WzEditorAssetGraphSnapshot, nodes) == 40);
static_assert(offsetof(WzEditorAssetGraphSnapshot, edges) == 56);
static_assert(offsetof(WzEditorAssetGraphSnapshot, zoom) == 72);

static_assert(sizeof(WzEditorSceneTransform) == 248);
static_assert(offsetof(WzEditorSceneTransform, translation_x) == 0);
static_assert(offsetof(WzEditorSceneTransform, rotation_quaternion_x) == 24);
static_assert(offsetof(WzEditorSceneTransform, rotation_euler_degrees_x) == 56);
static_assert(offsetof(WzEditorSceneTransform, scale_x) == 80);
static_assert(offsetof(WzEditorSceneTransform, display_translation_x) == 104);
static_assert(offsetof(WzEditorSceneTransform, display_translation_y) == 120);
static_assert(offsetof(WzEditorSceneTransform, display_translation_z) == 136);
static_assert(offsetof(WzEditorSceneTransform, display_rotation_x) == 152);
static_assert(offsetof(WzEditorSceneTransform, display_rotation_y) == 168);
static_assert(offsetof(WzEditorSceneTransform, display_rotation_z) == 184);
static_assert(offsetof(WzEditorSceneTransform, display_scale_x) == 200);
static_assert(offsetof(WzEditorSceneTransform, display_scale_y) == 216);
static_assert(offsetof(WzEditorSceneTransform, display_scale_z) == 232);

static_assert(sizeof(WzEditorSceneCamera) == 40);
static_assert(offsetof(WzEditorSceneCamera, fov_y) == 0);
static_assert(offsetof(WzEditorSceneCamera, flags) == 32);

static_assert(sizeof(WzEditorSceneRenderable) == 8);
static_assert(offsetof(WzEditorSceneRenderable, asset_graph_node_id) == 0);

static_assert(sizeof(WzEditorSceneRenderableSource) == 32);
static_assert(offsetof(WzEditorSceneRenderableSource, kind) == 0);
static_assert(offsetof(WzEditorSceneRenderableSource, display_name) == 16);

static_assert(sizeof(WzEditorSceneSceneSource) == 64);
static_assert(offsetof(WzEditorSceneSceneSource, kind) == 0);
static_assert(offsetof(WzEditorSceneSceneSource, path) == 16);
static_assert(offsetof(WzEditorSceneSceneSource, consume_mode) == 32);
static_assert(offsetof(WzEditorSceneSceneSource, scene_index) == 48);
static_assert(offsetof(WzEditorSceneSceneSource, style_override_count) == 52);
static_assert(offsetof(WzEditorSceneSceneSource, has_base_style) == 56);
static_assert(offsetof(WzEditorSceneSceneSource, reserved) == 60);

static_assert(sizeof(WzEditorSceneComponent) == 32);
static_assert(offsetof(WzEditorSceneComponent, kind) == 0);
static_assert(offsetof(WzEditorSceneComponent, display_name) == 16);

static_assert(sizeof(WzEditorSceneBehavior) == 104);
static_assert(offsetof(WzEditorSceneBehavior, id) == 0);
static_assert(offsetof(WzEditorSceneBehavior, label) == 16);
static_assert(offsetof(WzEditorSceneBehavior, module) == 32);
static_assert(offsetof(WzEditorSceneBehavior, name) == 48);
static_assert(offsetof(WzEditorSceneBehavior, enabled) == 64);
static_assert(offsetof(WzEditorSceneBehavior, events) == 72);
static_assert(offsetof(WzEditorSceneBehavior, config) == 88);

static_assert(sizeof(WzEditorSceneNode) == 512);
static_assert(offsetof(WzEditorSceneNode, id) == 0);
static_assert(offsetof(WzEditorSceneNode, display_name) == 16);
static_assert(offsetof(WzEditorSceneNode, parent_id) == 32);
static_assert(offsetof(WzEditorSceneNode, kind) == 48);
static_assert(offsetof(WzEditorSceneNode, flags) == 64);
static_assert(offsetof(WzEditorSceneNode, transform) == 72);
static_assert(offsetof(WzEditorSceneNode, camera) == 320);
static_assert(offsetof(WzEditorSceneNode, renderable) == 360);
static_assert(offsetof(WzEditorSceneNode, renderable_source) == 368);
static_assert(offsetof(WzEditorSceneNode, components) == 400);
static_assert(offsetof(WzEditorSceneNode, behaviors) == 416);
static_assert(offsetof(WzEditorSceneNode, children) == 432);
static_assert(offsetof(WzEditorSceneNode, scene_source) == 448);

static_assert(sizeof(WzEditorSceneSnapshot) == 72);
static_assert(offsetof(WzEditorSceneSnapshot, ok) == 0);
static_assert(offsetof(WzEditorSceneSnapshot, error) == 8);
static_assert(offsetof(WzEditorSceneSnapshot, schema) == 24);
static_assert(offsetof(WzEditorSceneSnapshot, roots) == 56);

static_assert(sizeof(WzEditorProjectSnapshot) == 200);
static_assert(offsetof(WzEditorProjectSnapshot, abi_version) == 0);
static_assert(offsetof(WzEditorProjectSnapshot, error) == 16);
static_assert(offsetof(WzEditorProjectSnapshot, project_name) == 32);
static_assert(offsetof(WzEditorProjectSnapshot, asset_graph) == 48);
static_assert(offsetof(WzEditorProjectSnapshot, scene) == 128);

static_assert(sizeof(WzEditorProjectCreate) == 32);
static_assert(offsetof(WzEditorProjectCreate, abi_version) == 0);
static_assert(offsetof(WzEditorProjectCreate, status) == 8);
static_assert(offsetof(WzEditorProjectCreate, error) == 16);

static_assert(sizeof(WzEditorAssetCatalogSchema) == 24);
static_assert(offsetof(WzEditorAssetCatalogSchema, schema) == 0);
static_assert(offsetof(WzEditorAssetCatalogSchema, label) == 8);

static_assert(sizeof(WzEditorAssetCatalogEntry) == 56);
static_assert(offsetof(WzEditorAssetCatalogEntry, type) == 0);
static_assert(offsetof(WzEditorAssetCatalogEntry, type_name) == 8);
static_assert(offsetof(WzEditorAssetCatalogEntry, category) == 24);
static_assert(offsetof(WzEditorAssetCatalogEntry, schemas) == 40);

static_assert(sizeof(WzEditorAssetCatalog) == 24);
static_assert(offsetof(WzEditorAssetCatalog, abi_version) == 0);
static_assert(offsetof(WzEditorAssetCatalog, entries) == 8);
#endif

WZ_ABI_API uint32_t wz_abi_version(void);

// Device-free authoring catalog: the asset-graph node types the editor can add,
// grouped by output type with their schemas. Excludes types whose GPU residency
// has not yet migrated to wozzits-rhi (#186). No project or session required.
// The blob's byte 0 is a WzEditorAssetCatalog. Caller frees with wz_free_buffer.
WZ_ABI_API WzResult wz_host_asset_catalog(WzBuffer* out_catalog);

WZ_ABI_API WzResult wz_host_load_project_snapshot(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    WzBuffer* out_snapshot);

WZ_ABI_API WzResult wz_host_create_project(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    const char* name_utf8,
    WzBuffer* out_project);

WZ_ABI_API WzResult wz_host_scene_set_node_properties(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    const char* node_id_utf8,
    const char* name_utf8,
    uint32_t visible);

WZ_ABI_API WzResult wz_host_scene_set_node_transform(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    const char* node_id_utf8,
    const char* translation_x_utf8,
    const char* translation_y_utf8,
    const char* translation_z_utf8,
    const char* rotation_x_utf8,
    const char* rotation_y_utf8,
    const char* rotation_z_utf8,
    const char* scale_x_utf8,
    const char* scale_y_utf8,
    const char* scale_z_utf8);

WZ_ABI_API WzResult wz_host_scene_set_camera(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    const char* node_id_utf8,
    const char* fov_y_utf8,
    const char* near_plane_utf8,
    const char* far_plane_utf8,
    const char* aspect_utf8);

WZ_ABI_API WzResult wz_host_asset_graph_set_node_position(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    uint64_t node_id,
    double x,
    double y);

WZ_ABI_API WzResult wz_host_asset_graph_set_zoom(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    double zoom);

WZ_ABI_API WzResult wz_host_open_project_session(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    WzHostSession** out_session);

WZ_ABI_API void wz_host_close_session(WzHostSession* session);

WZ_ABI_API WzResult wz_host_session_asset_graph_snapshot(
    WzHostSession* session,
    WzBuffer* out_snapshot);

WZ_ABI_API WzResult wz_host_asset_graph_can_connect(
    WzHostSession* session,
    uint64_t from_node_id,
    uint64_t to_node_id,
    uint32_t to_input_port,
    WzBuffer* out_check);

WZ_ABI_API WzResult wz_host_asset_graph_connect(
    WzHostSession* session,
    uint64_t from_node_id,
    uint64_t to_node_id,
    uint32_t to_input_port,
    WzBuffer* out_check);

WZ_ABI_API WzResult wz_host_asset_graph_disconnect_edge(
    WzHostSession* session,
    uint64_t edge_id);

// Add a new authored node for (schema, type) to the draft, with compiler
// default params, and return its new node id. Layout position is set separately
// (wz_host_asset_graph_set_node_position) by the editor after the snapshot.
WZ_ABI_API WzResult wz_host_asset_graph_add_node(
    WzHostSession* session,
    uint64_t schema,
    uint32_t type,
    uint64_t* out_node_id);

// Remove a node (and any edges touching it) from the draft.
WZ_ABI_API WzResult wz_host_asset_graph_remove_node(
    WzHostSession* session,
    uint64_t node_id);

// Set a node's layout position / the graph zoom in the session's in-memory
// layout (persisted on save). Session-based so freshly added, not-yet-saved
// nodes can be positioned.
WZ_ABI_API WzResult wz_host_session_set_node_position(
    WzHostSession* session,
    uint64_t node_id,
    double x,
    double y);

WZ_ABI_API WzResult wz_host_session_set_zoom(
    WzHostSession* session,
    double zoom);

// Set a string-valued node param (e.g. a shader "source_path") on the draft.
// Merges into the node's ParamBlock, invalidates its key, and marks it changed,
// so the next compile rebuilds it. CPU-only.
WZ_ABI_API WzResult wz_host_asset_graph_set_node_param_string(
    WzHostSession* session,
    uint64_t node_id,
    const char* name_utf8,
    const char* value_utf8);

// Persist the session's current draft to the project's asset-graph JSON on disk
// (preserving the existing "layout"). CPU-only; no engine runtime required.
WZ_ABI_API WzResult wz_host_session_save(WzHostSession* session);

// Start the in-process engine runtime for a project: spawns an engine-owned
// thread that creates the device + viewport window and renders until stopped.
// Returns NULL on invalid arguments or failure to start. GPU lives here, in the
// editor process - there is exactly one engine instance (Option Y, #189).
WZ_ABI_API WzHostRuntime* wz_host_runtime_start(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    WzHostLogCallback log_callback,
    void* log_user);

// Signal the runtime to stop, join its thread, and free it. Safe on NULL.
WZ_ABI_API void wz_host_runtime_stop(WzHostRuntime* runtime);

// Non-zero while the runtime's render thread is alive; zero once the viewport
// window has closed (or stop was serviced) and the thread exited, and zero for
// a NULL runtime. Lets the editor detect a closed viewport and offer a restart
// rather than binding into a dead handle.
WZ_ABI_API int wz_host_runtime_is_running(WzHostRuntime* runtime);

// Compile the session's current asset-graph draft on the running engine: copies
// the draft, binds it on the engine thread (materialize -> swap -> resolve ->
// rebind the renderer), and blocks until done. The draft is the only thing that
// crosses to the engine thread (Option Y, #189). Returns WZ_RESULT_OK on a
// clean compile; otherwise an error describing the failure.
WZ_ABI_API WzResult wz_host_runtime_bind_draft(
    WzHostRuntime* runtime,
    WzHostSession* session);

// Live scene-node transform edit posted to the running engine (non-blocking;
// applied on the engine thread's next frame, no disk write). Rotation is given
// as intrinsic Tait-Bryan euler angles in DEGREES (X, Y, Z), converted to a
// quaternion engine-side so that conversion never lives in the editor. Returns
// WZ_RESULT_OK once queued; WZ_RESULT_INVALID_ARGUMENT for a null runtime or an
// empty node id.
WZ_ABI_API WzResult wz_host_runtime_set_node_transform(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    double translation_x,
    double translation_y,
    double translation_z,
    double rotation_x_degrees,
    double rotation_y_degrees,
    double rotation_z_degrees,
    double scale_x,
    double scale_y,
    double scale_z);

// Live node label/visibility edit posted to the running engine (non-blocking,
// applied on the engine thread's next frame, no disk write). The renderer skips
// invisible nodes, so this hides/shows live. WZ_RESULT_INVALID_ARGUMENT for a
// null runtime or an empty node id.
WZ_ABI_API WzResult wz_host_runtime_set_node_properties(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* name_utf8,
    uint32_t visible);

// Live reparent posted to the running engine (non-blocking, applied next frame,
// no disk write). new_parent_id_utf8 NULL/empty => detach to top level. The
// engine ignores an invalid reparent (missing node, self, or cycle); the editor
// pre-validates for drag UX. WZ_RESULT_INVALID_ARGUMENT for a null runtime or an
// empty node id.
WZ_ABI_API WzResult wz_host_runtime_reparent_node(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* new_parent_id_utf8);

// Live delete posted to the running engine (non-blocking, applied next frame).
// Removes the node and its subtree. WZ_RESULT_INVALID_ARGUMENT for a null
// runtime or an empty node id.
WZ_ABI_API WzResult wz_host_runtime_remove_node(
    WzHostRuntime* runtime,
    const char* node_id_utf8);

// Persist the running scene to its source file (non-blocking; the engine saves
// on its next frame). No-op if nothing changed since load/last save. Also done
// automatically when the runtime exits. WZ_RESULT_INVALID_ARGUMENT for a null
// runtime.
WZ_ABI_API WzResult wz_host_runtime_save_scene(WzHostRuntime* runtime);

// Reload the project's behavior-module DLLs into the running runtime (after the
// editor recompiled them), without restarting the engine: the engine clears its
// behavior registry, re-registers the built-ins, reloads every DLL in the
// project's behavior_module_folder, and rebuilds the behavior scene on its next
// frame. Non-blocking; per-module load results are written to the engine log.
// WZ_RESULT_INVALID_ARGUMENT for a null runtime.
WZ_ABI_API WzResult wz_host_runtime_reload_behavior_modules(
    WzHostRuntime* runtime);

// Names of every currently registered behavior module (built-ins + the project
// DLLs), so the editor can offer them when adding a behavior binding. Returns a
// newline-delimited UTF-8 list in out_modules (free with wz_free_buffer); an
// empty buffer means no modules are registered. WZ_RESULT_INVALID_ARGUMENT for a
// null runtime.
WZ_ABI_API WzResult wz_host_runtime_behavior_module_catalog(
    WzHostRuntime* runtime,
    WzBuffer* out_modules);

// Add a child node under `parent_id_utf8` (NULL/empty => top level) in the
// running scene, blocking until the engine thread applies it, and return the
// minted counter id in out_new_id (UTF-8; free with wz_free_buffer).
// WZ_RESULT_INVALID_ARGUMENT for a null runtime or a missing parent.
WZ_ABI_API WzResult wz_host_runtime_add_child_node(
    WzHostRuntime* runtime,
    const char* parent_id_utf8,
    WzBuffer* out_new_id);

// ─── Live behavior-binding authoring on the running runtime ─────────────────
// These mutate a scene node's authored behavior binding(s) (SceneBehaviorAsset)
// on the running engine. Like the live scene edits above, the mutation is
// DEFERRED: it is queued and applied on the engine thread's NEXT frame, never
// synchronously mid-frame — the safety property the "one ABI" depends on. After
// applying, the engine re-materializes the behavior runtime so the change takes
// effect, and marks the scene dirty (persisted on save / exit).
//
// HOST-CAPABILITY GATE: these are mutation verbs, gated behind the host role.
// The editor-started runtime (wz_host_runtime_start) holds the capability, so
// the editor may call them; a caller without the host role (e.g. a future
// behavior-as-consumer of this same ABI) is rejected fail-closed with
// WZ_RESULT_INVALID_ARGUMENT. The gate is enforced in the implementation
// (require_host_scene_authoring); see wozzits_abi.cpp.

// Add a behavior binding (the given `module`) to node `node_id_utf8`, minting a
// stable binding id (a deduped slug from the node id) and returning it in
// out_binding_id (UTF-8; free with wz_free_buffer). Unlike the other behavior
// verbs this BLOCKS until the engine thread applies it, because the host UI
// needs the minted id back — which is safe because the caller is the host's UI
// thread, not a behavior on the engine thread. WZ_RESULT_INVALID_ARGUMENT for a
// null runtime, an empty node id/module, a missing node, or a non-host caller.
WZ_ABI_API WzResult wz_host_runtime_add_node_behavior(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* module_utf8,
    WzBuffer* out_binding_id);

// Remove the behavior binding `binding_id_utf8` from node `node_id_utf8`
// (non-blocking, applied next frame). WZ_RESULT_INVALID_ARGUMENT for a null
// runtime, an empty node/binding id, or a non-host caller.
WZ_ABI_API WzResult wz_host_runtime_remove_node_behavior(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* binding_id_utf8);

// ─── Live optional-component authoring on the running runtime ───────────────
// Add/remove one of the four editor-managed OPTIONAL node components by a kind
// token. `kind_utf8` is one of: "camera", "proximity", "collision", "motion".
// Both verbs are DEFERRED (queued and applied on the engine thread's NEXT frame,
// never synchronously) and NON-BLOCKING (no result crosses back) — the same
// safety property the behavior verbs rely on. Add default-constructs the
// component slot (sensible defaults); remove clears it. The change marks the
// scene dirty (persisted on save/exit); the renderer reads the live scene each
// frame so it takes effect on the next render. None of these four participates
// in the behavior runtime, so neither rebuilds it.
//
// HOST-CAPABILITY GATE: like the behavior verbs these are mutation verbs gated
// behind the host role (require_host_scene_authoring). An unknown kind is
// rejected fail-closed. WZ_RESULT_INVALID_ARGUMENT for a null runtime, an empty
// node id/kind, an unknown kind, or a non-host caller.
//
// The Renderable component is NOT covered here: the legacy embedded renderable
// slot is a compat/debug path that is not editor-authorable, and the PREFERRED
// asset-graph-backed renderable is authored by
// wz_host_runtime_set_node_renderable_asset below.
WZ_ABI_API WzResult wz_host_runtime_add_node_component(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* kind_utf8);

WZ_ABI_API WzResult wz_host_runtime_remove_node_component(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* kind_utf8);

// Author the PREFERRED asset-graph-backed Renderable component on a node: bind
// `node_id_utf8` to the authored asset-graph node `asset_graph_node_id`, or
// CLEAR the renderable when `asset_graph_node_id == 0`. The resolved AssetKey is
// reset so it re-resolves from the (new or absent) node id; the legacy embedded
// renderable slot is never touched. DEFERRED (applied on the engine thread's
// next frame) and NON-BLOCKING, like the component verbs. Marks the scene dirty;
// the renderer reads the live scene each frame so it takes effect on the next
// render, and the behavior runtime is not rebuilt. An unknown/missing node is a
// logged engine-thread no-op.
//
// HOST-CAPABILITY GATE: a mutation verb gated behind the host role
// (require_host_scene_authoring). WZ_RESULT_INVALID_ARGUMENT for a null runtime,
// an empty node id, or a non-host caller.
WZ_ABI_API WzResult wz_host_runtime_set_node_renderable_asset(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    uint64_t asset_graph_node_id);

// Author the PREFERRED asset-graph-backed Scene-source component on node
// `node_id_utf8` (issue #213): point it at the authored "Scene from GLB"
// asset-graph node `asset_graph_node_id`, or CLEAR the scene source when the id
// is 0. `consume_mode` selects how the referenced Scene is consumed:
//   0 (WZ_SCENE_SOURCE_INSTANCE) — keep a live reference; the engine grafts the
//       referenced sub-scene's GLB-named nodes as children of the host each
//       (re)bind (the host transform sizes/places the whole sub-tree; each child
//       is a real scene entity, individually drivable by the behavior API).
//   1 (WZ_SCENE_SOURCE_FLATTEN)  — expand the referenced sub-scene's nodes into
//       the host's tree ONCE as real, editable nodes and drop the live reference.
// consume_mode is ignored when clearing (id == 0). DEFERRED (applied on the
// engine thread's next frame) and NON-BLOCKING, like the renderable verb. Marks
// the scene dirty. An unknown/missing node is a logged engine-thread no-op.
//
// NOTE: this is a NEW exported function (no ABI snapshot struct change). The
// scene snapshot surfaces the glb_scene_source DESCRIPTOR summary (#213 Phase 2,
// WzEditorSceneNode.scene_source), but this verb's node-ref scene_source resolved
// key and the runtime-grafted children are still not surfaced (the children are
// runtime-only and absent from the JSON-reparse snapshot path).
//
// HOST-CAPABILITY GATE: a mutation verb gated behind the host role
// (require_host_scene_authoring). WZ_RESULT_INVALID_ARGUMENT for a null runtime,
// an empty node id, or a non-host caller.
#define WZ_SCENE_SOURCE_INSTANCE 0u
#define WZ_SCENE_SOURCE_FLATTEN 1u
WZ_ABI_API WzResult wz_host_runtime_set_node_scene_source(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    uint64_t asset_graph_node_id,
    uint32_t consume_mode);

// Author the self-contained GLB scene-source DESCRIPTOR on node `node_id_utf8`
// (issue #213, the asset-graph-INDEPENDENT route — the terrain/mesh-source model,
// distinct from the node-ref verb above). The descriptor is plain data that
// persists as JSON and is re-resolved on every load/rebind (no asset-graph node
// involved): a `glb_path_utf8` (resource-relative, e.g. "gltf/tank1.glb"), the
// `scene_index` within the GLB, and `consume_mode` (the same WZ_SCENE_SOURCE_*
// tokens as the node-ref verb). At apply the engine re-resolves the descriptor
// into a "Scene from GLB" asset and grafts (Instance) the host's children. An
// empty/NULL `glb_path_utf8` CLEARS the descriptor. Phase 3a authors a single
// default render style only (no per-component styling — that is Phase 3b).
// DEFERRED (applied on the engine thread's next frame) and NON-BLOCKING, like the
// node-ref scene-source verb. Marks the scene dirty. An unknown/missing node is a
// logged engine-thread no-op.
//
// NOTE: like wz_host_runtime_set_node_scene_source this is a NEW exported function
// with NO ABI snapshot struct change, so WZ_ABI_VERSION is unchanged. The scene
// snapshot already surfaces the resulting glb_scene_source DESCRIPTOR summary
// (#213 Phase 2, WzEditorSceneNode.scene_source), so the editor can read back what
// this verb authored.
//
// HOST-CAPABILITY GATE: a mutation verb gated behind the host role
// (require_host_scene_authoring). WZ_RESULT_INVALID_ARGUMENT for a null runtime,
// an empty node id, or a non-host caller.
WZ_ABI_API WzResult wz_host_runtime_set_node_glb_scene_source(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* glb_path_utf8,
    uint32_t scene_index,
    uint32_t consume_mode);

// Set a behavior binding's enabled flag (non-blocking). A disabled binding does
// not dispatch. WZ_RESULT_INVALID_ARGUMENT for a null runtime, an empty
// node/binding id, or a non-host caller.
WZ_ABI_API WzResult wz_host_runtime_set_node_behavior_enabled(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* binding_id_utf8,
    uint32_t enabled);

// Set a behavior binding's label + module (non-blocking). NULL label/module are
// treated as empty. WZ_RESULT_INVALID_ARGUMENT for a null runtime, an empty
// node/binding id, or a non-host caller.
WZ_ABI_API WzResult wz_host_runtime_set_node_behavior_fields(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* binding_id_utf8,
    const char* label_utf8,
    const char* module_utf8);

// Replace a behavior binding's events list (non-blocking). `events_utf8` is a
// single newline-delimited UTF-8 string (one channel token per line, e.g.
// "frame.update\ninput.action"); it is parsed engine-side. Empty/NULL clears
// the list (the binding then falls back to its module's default channels).
// WZ_RESULT_INVALID_ARGUMENT for a null runtime, an empty node/binding id, or a
// non-host caller.
WZ_ABI_API WzResult wz_host_runtime_set_node_behavior_events(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* binding_id_utf8,
    const char* events_utf8);

// Set/replace one config entry (by `key_utf8`) on a behavior binding
// (non-blocking). `kind_utf8` is "bool" | "int" | "float" | "string"; the value
// is parsed from `value_utf8` per kind (int/float store as Number). An existing
// entry with the same key is overwritten. WZ_RESULT_INVALID_ARGUMENT for a null
// runtime, an empty node/binding id/key, an unknown kind, or a non-host caller.
WZ_ABI_API WzResult wz_host_runtime_set_node_behavior_config(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* binding_id_utf8,
    const char* key_utf8,
    const char* kind_utf8,
    const char* value_utf8);

// Remove one config entry (by `key_utf8`) from a behavior binding
// (non-blocking). WZ_RESULT_INVALID_ARGUMENT for a null runtime, an empty
// node/binding id/key, or a non-host caller.
WZ_ABI_API WzResult wz_host_runtime_clear_node_behavior_config(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* binding_id_utf8,
    const char* key_utf8);

WZ_ABI_API void wz_free_buffer(WzBuffer* buffer);

#ifdef __cplusplus
}
#endif
