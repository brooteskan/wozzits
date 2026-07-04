#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 28 -> 29 (issue #230): WzEditorSceneNode grew the custom-renderable
// ingredient tables (renderable_bindings / renderable_constants) — a struct
// layout change, so readers must match.
#define WZ_ABI_VERSION 29u

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
    // Authored render-binding refs (issue #213), surfaced so the inspector reveals
    // + pre-selects these sections from persisted state. Distinct from
    // HAS_SCENE_SOURCE (the GLB descriptor summary above): HAS_SCENE_SOURCE_REF is
    // the "Subtree from asset" asset-graph node ref (scene_source_node_id).
    WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE_REF = 1u << 8u,
    WZ_EDITOR_SCENE_NODE_HAS_GEOMETRY = 1u << 9u,
    WZ_EDITOR_SCENE_NODE_HAS_RENDER_PROGRAM = 1u << 10u,
    // Persisted Collision/Motion component field values (read-back gap fix): when
    // set, the node's `collision`/`motion` struct carries the authored field
    // values so the inspector restores them on select + after reload (without
    // these the components surfaced presence-only, resetting fields to defaults).
    WZ_EDITOR_SCENE_NODE_HAS_COLLISION = 1u << 11u,
    WZ_EDITOR_SCENE_NODE_HAS_MOTION = 1u << 12u,
    WZ_EDITOR_SCENE_NODE_HAS_AUDIO_SOURCE = 1u << 13u,
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

// The high-impact subset of a MeshRenderStyleData packed for read-back +
// re-authoring of a GLB component's style (issue #213 Phase 3b-2): surface +
// wireframe enabled flags (0/1) and RGBA colors. The rest of MeshRenderStyleData
// is not surfaced (it stays at engine defaults; not editor-authorable here).
typedef struct WzEditorGlbStyle
{
    uint32_t surface_enabled;     // 0/1
    float surface_rgba[4];
    uint32_t wireframe_enabled;   // 0/1
    float wireframe_rgba[4];
} WzEditorGlbStyle;

// One per-mesh-index style override (issue #213 Phase 3b-2), an entry of
// WzEditorSceneSceneSource.style_overrides. `mesh_index` is the GLB mesh index
// (matching WzEditorGlbComponent.mesh_index) the override applies to.
typedef struct WzEditorGlbStyleOverride
{
    uint32_t mesh_index;
    uint32_t reserved;
    WzEditorGlbStyle style;
} WzEditorGlbStyleOverride;

// Read-only summary of a node's GLB scene-source descriptor (issue #213),
// present only when WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE is set on the node.
// has_base_style is 0/1 and style_override_count is the override array size; the
// editor-authorable style subset is also packed (Phase 3b-2): base_style (valid
// iff has_base_style) and the style_overrides table of WzEditorGlbStyleOverride
// (count matches style_override_count), so the editor pre-fills its style editor.
typedef struct WzEditorSceneSceneSource
{
    WzEditorStringSpan kind;          // "glb"
    WzEditorStringSpan path;
    WzEditorStringSpan consume_mode;  // "instance" | "flatten"
    uint32_t scene_index;
    uint32_t style_override_count;
    uint32_t has_base_style;          // 0/1
    uint32_t reserved;
    WzEditorGlbStyle base_style;      // valid iff has_base_style
    WzEditorTableSpan style_overrides; // WzEditorGlbStyleOverride[]
} WzEditorSceneSceneSource;

// Authored Collision-component field values surfaced read-back so the inspector
// restores them on select + after reload (present iff WZ_EDITOR_SCENE_NODE_HAS_
// COLLISION). `has_collision_ref` is 0/1; `collision_asset_node_id` is the
// authored asset-graph collision node id (valid iff has_collision_ref).
// `constrain_movement` is 0/1.
typedef struct WzEditorSceneCollision
{
    uint32_t collision_asset_node_id;
    uint8_t has_collision_ref;
    uint8_t constrain_movement;
    uint8_t reserved0;
    uint8_t reserved1;
} WzEditorSceneCollision;

// Authored Motion-component field values surfaced read-back so the inspector
// restores them on select + after reload (present iff WZ_EDITOR_SCENE_NODE_HAS_
// MOTION). The terrain-stick subset: `terrain_constrained`/`align_to_surface`
// are 0/1; ride/footprint/strength are floats.
typedef struct WzEditorSceneMotion
{
    uint8_t terrain_constrained;
    uint8_t align_to_surface;
    uint8_t reserved0;
    uint8_t reserved1;
    float ride_height;
    float footprint_radius;
    float alignment_strength;
} WzEditorSceneMotion;

// Authored AudioSource-component field values surfaced read-back so the inspector
// restores them on select + after reload (present iff WZ_EDITOR_SCENE_NODE_HAS_
// AUDIO_SOURCE). `has_renderable_ref` is 0/1; `audio_renderable_node_id` is the
// authored audio-renderable asset-graph node id (valid iff has_renderable_ref).
// `auto_play` / `enabled` are 0/1.
typedef struct WzEditorSceneAudioSource
{
    uint64_t audio_renderable_node_id;
    uint8_t has_renderable_ref;
    uint8_t auto_play;
    uint8_t enabled;
    uint8_t reserved0;
    uint32_t reserved1;
} WzEditorSceneAudioSource;

typedef struct WzEditorSceneComponent
{
    WzEditorStringSpan kind;
    WzEditorStringSpan display_name;
} WzEditorSceneComponent;

// One authored semantic resource binding of a node's custom-renderable
// ingredients (issue #229/#230), an entry of
// WzEditorSceneNode.renderable_bindings. `semantic` names a row of the
// effective program's authored binding layout; `has_source_ref` is 0/1 and
// asset_graph_node_id (the authored source anchor) is valid iff set. Only the
// authored id is surfaced — the resolved key re-bridges on bind.
typedef struct WzEditorSceneRenderableBinding
{
    WzEditorStringSpan semantic;
    uint64_t asset_graph_node_id;
    uint32_t has_source_ref;
    uint32_t reserved;
} WzEditorSceneRenderableBinding;

// One per-instance constant override of a node's custom-renderable
// ingredients (issue #229/#230), an entry of
// WzEditorSceneNode.renderable_constants: a value for one of the program
// layout's declared constant tail fields. A field narrower than 4 floats
// consumes a prefix of `value`.
typedef struct WzEditorSceneRenderableConstant
{
    WzEditorStringSpan name;
    float value[4];
} WzEditorSceneRenderableConstant;

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
    // Authored render-binding refs (issue #213), each valid iff its HAS_* flag is
    // set: the "Subtree from asset" node ref and the render-binding geometry +
    // render-program asset-graph node ids. Appended last so existing field offsets
    // are unchanged. Only the authored node id is surfaced (resolved keys re-bridge
    // on bind).
    uint64_t scene_source_node_id;
    uint64_t geometry_node_id;
    uint64_t render_program_node_id;
    // Persisted Collision/Motion component field values (read-back gap fix), each
    // valid iff its HAS_* flag is set. Appended last so existing field offsets are
    // unchanged.
    WzEditorSceneCollision collision;
    WzEditorSceneMotion motion;
    // Persisted AudioSource component field values (read-back), valid iff
    // WZ_EDITOR_SCENE_NODE_HAS_AUDIO_SOURCE. Appended last so existing offsets
    // are unchanged.
    WzEditorSceneAudioSource audio_source;
    // Custom-renderable ingredients (issue #229/#230), always-valid tables
    // (possibly empty — presence is the count, no HAS_* flag):
    // renderable_bindings is WzEditorSceneRenderableBinding[] and
    // renderable_constants is WzEditorSceneRenderableConstant[]. Appended last
    // so existing field offsets are unchanged (the node STRIDE changed —
    // that is the WZ_ABI_VERSION 29 bump).
    WzEditorTableSpan renderable_bindings;
    WzEditorTableSpan renderable_constants;
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

// ─── GLB scene-source hierarchy (issue #213, Phase 3b-1) ────────────────────
// On-demand, READ-ONLY import of a GLB scene's component hierarchy so the editor
// can show what a node's glb_scene_source descriptor will graft as children. The
// grafted children are runtime-only and absent from the JSON-reparse snapshot, so
// this is a separate query (wz_import_glb_scene_hierarchy) returning the flat node
// list; the editor links the tree by parent_id. No styling/mutation (that is
// Phase 3b-2) and no `local` transform is packed.

typedef uint32_t WzEditorGlbComponentFlags;
enum
{
    WZ_EDITOR_GLB_COMPONENT_HAS_PARENT = 1u << 0u,
    WZ_EDITOR_GLB_COMPONENT_HAS_MESH = 1u << 1u,
};

typedef struct WzEditorGlbComponent
{
    WzEditorStringSpan id;
    WzEditorStringSpan name;
    WzEditorStringSpan parent_id;   // valid iff HAS_PARENT
    WzEditorGlbComponentFlags flags;
    uint32_t mesh_index;            // valid iff HAS_MESH
    uint32_t node_index;
    uint32_t reserved;
} WzEditorGlbComponent;

typedef struct WzEditorGlbSceneHierarchy
{
    uint32_t abi_version;
    uint32_t ok;
    WzEditorStringSpan error;
    WzEditorStringSpan scene_name;
    uint32_t scene_index;
    uint32_t reserved;
    WzEditorTableSpan components;    // WzEditorGlbComponent[] (flat; parent_id links the tree)
} WzEditorGlbSceneHierarchy;

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

static_assert(sizeof(WzEditorGlbStyle) == 40);
static_assert(offsetof(WzEditorGlbStyle, surface_enabled) == 0);
static_assert(offsetof(WzEditorGlbStyle, surface_rgba) == 4);
static_assert(offsetof(WzEditorGlbStyle, wireframe_enabled) == 20);
static_assert(offsetof(WzEditorGlbStyle, wireframe_rgba) == 24);

static_assert(sizeof(WzEditorGlbStyleOverride) == 48);
static_assert(offsetof(WzEditorGlbStyleOverride, mesh_index) == 0);
static_assert(offsetof(WzEditorGlbStyleOverride, reserved) == 4);
static_assert(offsetof(WzEditorGlbStyleOverride, style) == 8);

static_assert(sizeof(WzEditorSceneSceneSource) == 120);
static_assert(offsetof(WzEditorSceneSceneSource, kind) == 0);
static_assert(offsetof(WzEditorSceneSceneSource, path) == 16);
static_assert(offsetof(WzEditorSceneSceneSource, consume_mode) == 32);
static_assert(offsetof(WzEditorSceneSceneSource, scene_index) == 48);
static_assert(offsetof(WzEditorSceneSceneSource, style_override_count) == 52);
static_assert(offsetof(WzEditorSceneSceneSource, has_base_style) == 56);
static_assert(offsetof(WzEditorSceneSceneSource, reserved) == 60);
static_assert(offsetof(WzEditorSceneSceneSource, base_style) == 64);
static_assert(offsetof(WzEditorSceneSceneSource, style_overrides) == 104);

static_assert(sizeof(WzEditorSceneCollision) == 8);
static_assert(offsetof(WzEditorSceneCollision, collision_asset_node_id) == 0);
static_assert(offsetof(WzEditorSceneCollision, has_collision_ref) == 4);
static_assert(offsetof(WzEditorSceneCollision, constrain_movement) == 5);
static_assert(offsetof(WzEditorSceneCollision, reserved0) == 6);
static_assert(offsetof(WzEditorSceneCollision, reserved1) == 7);

static_assert(sizeof(WzEditorSceneMotion) == 16);
static_assert(offsetof(WzEditorSceneMotion, terrain_constrained) == 0);
static_assert(offsetof(WzEditorSceneMotion, align_to_surface) == 1);
static_assert(offsetof(WzEditorSceneMotion, reserved0) == 2);
static_assert(offsetof(WzEditorSceneMotion, reserved1) == 3);
static_assert(offsetof(WzEditorSceneMotion, ride_height) == 4);
static_assert(offsetof(WzEditorSceneMotion, footprint_radius) == 8);
static_assert(offsetof(WzEditorSceneMotion, alignment_strength) == 12);

static_assert(sizeof(WzEditorSceneAudioSource) == 16);
static_assert(offsetof(WzEditorSceneAudioSource, audio_renderable_node_id) == 0);
static_assert(offsetof(WzEditorSceneAudioSource, has_renderable_ref) == 8);
static_assert(offsetof(WzEditorSceneAudioSource, auto_play) == 9);
static_assert(offsetof(WzEditorSceneAudioSource, enabled) == 10);
static_assert(offsetof(WzEditorSceneAudioSource, reserved0) == 11);
static_assert(offsetof(WzEditorSceneAudioSource, reserved1) == 12);

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

static_assert(sizeof(WzEditorSceneRenderableBinding) == 32);
static_assert(offsetof(WzEditorSceneRenderableBinding, semantic) == 0);
static_assert(
    offsetof(WzEditorSceneRenderableBinding, asset_graph_node_id) == 16);
static_assert(offsetof(WzEditorSceneRenderableBinding, has_source_ref) == 24);

static_assert(sizeof(WzEditorSceneRenderableConstant) == 32);
static_assert(offsetof(WzEditorSceneRenderableConstant, name) == 0);
static_assert(offsetof(WzEditorSceneRenderableConstant, value) == 16);

static_assert(sizeof(WzEditorSceneNode) == 664);
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
static_assert(offsetof(WzEditorSceneNode, scene_source_node_id) == 568);
static_assert(offsetof(WzEditorSceneNode, geometry_node_id) == 576);
static_assert(offsetof(WzEditorSceneNode, render_program_node_id) == 584);
static_assert(offsetof(WzEditorSceneNode, collision) == 592);
static_assert(offsetof(WzEditorSceneNode, motion) == 600);
static_assert(offsetof(WzEditorSceneNode, audio_source) == 616);
static_assert(offsetof(WzEditorSceneNode, renderable_bindings) == 632);
static_assert(offsetof(WzEditorSceneNode, renderable_constants) == 648);

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

static_assert(sizeof(WzEditorGlbComponent) == 64);
static_assert(offsetof(WzEditorGlbComponent, id) == 0);
static_assert(offsetof(WzEditorGlbComponent, name) == 16);
static_assert(offsetof(WzEditorGlbComponent, parent_id) == 32);
static_assert(offsetof(WzEditorGlbComponent, flags) == 48);
static_assert(offsetof(WzEditorGlbComponent, mesh_index) == 52);
static_assert(offsetof(WzEditorGlbComponent, node_index) == 56);
static_assert(offsetof(WzEditorGlbComponent, reserved) == 60);

static_assert(sizeof(WzEditorGlbSceneHierarchy) == 64);
static_assert(offsetof(WzEditorGlbSceneHierarchy, abi_version) == 0);
static_assert(offsetof(WzEditorGlbSceneHierarchy, ok) == 4);
static_assert(offsetof(WzEditorGlbSceneHierarchy, error) == 8);
static_assert(offsetof(WzEditorGlbSceneHierarchy, scene_name) == 24);
static_assert(offsetof(WzEditorGlbSceneHierarchy, scene_index) == 40);
static_assert(offsetof(WzEditorGlbSceneHierarchy, reserved) == 44);
static_assert(offsetof(WzEditorGlbSceneHierarchy, components) == 48);
#endif

WZ_ABI_API uint32_t wz_abi_version(void);

// Device-free authoring catalog: the asset-graph node types the editor can add,
// grouped by output type with their schemas. Excludes types whose GPU residency
// has not yet migrated to wozzits-rhi (#186). No project or session required.
// The blob's byte 0 is a WzEditorAssetCatalog. Caller frees with wz_free_buffer.
WZ_ABI_API WzResult wz_host_asset_catalog(WzBuffer* out_catalog);

// Device-free, READ-ONLY import of a GLB scene's component hierarchy (issue #213,
// Phase 3b-1): the engine roots `glb_path_utf8` against `resource_root_utf8` using
// the shared file-carrier convention (the editor passes the node's scene_source
// path verbatim — relative or absolute, optionally quote-wrapped — and never
// reimplements the rooting), reads the file, runs the pure-CPU glTF scene importer
// for `scene_index`, and packs the flat component list into a blob whose byte 0 is
// a WzEditorGlbSceneHierarchy. A null/empty resource root roots relative paths
// against the process working directory. No runtime, project, or host capability
// required — this only reads bytes and parses them. A null/empty path, an
// unreadable file, or an import failure returns a blob with ok = 0 and an `error`
// string (never crashes). Caller frees with wz_free_buffer.
WZ_ABI_API WzBuffer wz_import_glb_scene_hierarchy(
    const char* glb_path_utf8,
    const char* resource_root_utf8,
    uint32_t scene_index);

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

// Swap the working scene to `scene_path_utf8` -- the prefab editor's "open a
// scenelet" (from wz_host_runtime_scenelet_catalog). Reuses the project asset
// graph + modules, so the scenelet renders against the same graph. Blocks until
// the engine thread has loaded it. After it returns OK the editor should
// re-snapshot the scene (its nodes are now the scenelet's); save_scene then
// persists edits back to that file. An EMPTY/null path reopens the project's MAIN
// scene (switch back from a scenelet) without the caller tracking its path.
// WZ_RESULT_INVALID_ARGUMENT for a null runtime.
WZ_ABI_API WzResult wz_host_runtime_open_scene(
    WzHostRuntime* runtime,
    const char* scene_path_utf8);

// Carve the subtree rooted at `root_node_id_utf8` out of the running scene and
// write it to `out_path_utf8` as a standalone prefab scene.json (the prefab-
// system milestone): the root node + its transitive descendants become the new
// scene's nodes, the root re-rooted to the origin so a future spawn places the
// prefab purely by the spawn transform. Runtime-grafted scene_source children
// (#213) are excluded. `out_path_utf8` is resolved like the scene source path
// (absolute as-is, else joined onto the project resource root). Blocks until the
// engine thread completes it (it owns the scene + the write). WZ_RESULT_OK on
// success; WZ_RESULT_INVALID_ARGUMENT for a null runtime, a caller without the
// host capability, or an empty root id / output path; WZ_RESULT_INTERNAL_ERROR
// if the root id does not resolve or the write fails.
WZ_ABI_API WzResult wz_host_export_subtree_as_scene(
    WzHostRuntime* runtime,
    const char* root_node_id_utf8,
    const char* out_path_utf8);

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

// The project's scenelets (prefabs), for the editor's scenelet menu. Returns a
// newline-delimited UTF-8 list in out_scenelets (free with wz_free_buffer); each
// line is "name\tpath" (the prefab name and its resource-relative scene-file path,
// tab-separated). An empty buffer means the project has no scenelets. Gathered when
// the scene loaded. WZ_RESULT_INVALID_ARGUMENT for a null runtime.
WZ_ABI_API WzResult wz_host_runtime_scenelet_catalog(
    WzHostRuntime* runtime,
    WzBuffer* out_scenelets);

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

// Author an AudioSource's renderable reference (the audio analog of
// set_node_renderable_asset): bind `node_id_utf8` to the authored audio-renderable
// asset-graph node `asset_graph_node_id`, creating the AudioSource component on a
// non-zero pick, or CLEAR the reference when 0 (the component remains). The
// resolved key re-resolves from the node id at bind. DEFERRED + NON-BLOCKING;
// marks the scene dirty. HOST-CAPABILITY GATE (require_host_scene_authoring):
// WZ_RESULT_INVALID_ARGUMENT for a null runtime, empty node id, or non-host
// caller. NEW exported fn — WZ_ABI_VERSION unchanged (no struct change).
WZ_ABI_API WzResult wz_host_runtime_set_node_audio_renderable(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    uint64_t asset_graph_node_id);

// Set an AudioSource's per-entity play policy (auto_play / enabled). DEFERRED +
// NON-BLOCKING; marks the scene dirty. No-op on the engine thread if the node has
// no AudioSource. HOST-CAPABILITY GATE (require_host_scene_authoring):
// WZ_RESULT_INVALID_ARGUMENT for a null runtime, empty node id, or non-host
// caller. NEW exported fn — WZ_ABI_VERSION unchanged (no struct change).
WZ_ABI_API WzResult wz_host_runtime_set_node_audio_source_play(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    uint8_t auto_play,
    uint8_t enabled);

// Author the GEOMETRY half of node `node_id_utf8`'s renderable binding (issue
// #213 increment 2): point it at the authored geometry asset-graph node
// `asset_graph_node_id`, or clear it (the node stops drawing) when 0. DEFERRED
// (applied on the engine's next frame) + NON-BLOCKING, like the renderable verb.
// Marks the scene dirty; the engine re-assembles the binding (the render program
// inherited down the scene tree) and the renderer reflects it next render. An
// unknown/missing node is a logged engine-thread no-op. HOST-CAPABILITY GATE
// (require_host_scene_authoring): WZ_RESULT_INVALID_ARGUMENT for a null runtime,
// an empty node id, or a non-host caller. NEW exported fn — WZ_ABI_VERSION
// unchanged (no struct change).
WZ_ABI_API WzResult wz_host_runtime_set_node_geometry_asset(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    uint64_t asset_graph_node_id);

// Author the RENDER-PROGRAM half of node `node_id_utf8`'s renderable binding
// (issue #213 increment 2): point it at the authored render-program asset-graph
// node `asset_graph_node_id`, or clear it when 0. The program is INHERITED down
// the scene tree, so this cascades to descendants without their own program.
// Same deferral, gating, and version note as
// wz_host_runtime_set_node_geometry_asset.
WZ_ABI_API WzResult wz_host_runtime_set_node_render_program(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    uint64_t asset_graph_node_id);

// Author ONE semantic resource binding of node `node_id_utf8`'s
// custom-renderable ingredients (issue #229/#230): upsert the row for layout
// semantic `semantic_utf8` pointing at the authored asset-graph source
// `asset_graph_node_id`, or REMOVE the row when the id is 0. A binding present
// makes the assembled renderable the CUSTOM (0x70A) form, so the engine
// re-assembles + re-resolves like the geometry/program verbs; a binding the
// program's layout cannot satisfy surfaces its reason on the synthesized
// asset (log + resolve detail), never a silent fallback. Same deferral and
// gating as wz_host_runtime_set_node_geometry_asset. NEW exported fn (the
// WZ_ABI_VERSION 29 bump is the WzEditorSceneNode table growth, not this).
WZ_ABI_API WzResult wz_host_runtime_set_node_renderable_binding(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* semantic_utf8,
    uint64_t asset_graph_node_id);

// Author ONE per-instance constant override of node `node_id_utf8`'s
// custom-renderable ingredients (issue #229/#230): upsert `name_utf8` with
// `value_xyzw` (4 floats; a narrower declared field consumes a prefix), or
// REMOVE the override when `value_xyzw` is null. Overrides merge over the
// synthesized recipe's defaults at PACK time — no re-assembly, no asset-graph
// recompile, no re-key; the next rendered frame reflects the edit (the ONLY
// exception: the first override on a node with no bindings flips its
// renderable to the custom form, which re-assembles). Same deferral and
// gating as wz_host_runtime_set_node_geometry_asset. NEW exported fn (the
// WZ_ABI_VERSION 29 bump is the WzEditorSceneNode table growth, not this).
WZ_ABI_API WzResult wz_host_runtime_set_node_renderable_param(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    const char* name_utf8,
    const float* value_xyzw);

// Author node `node_id_utf8`'s Collision component by REFERENCE (issue
// #216/#217): point it at an authored asset-graph collision node
// `asset_graph_node_id` (e.g. collision_from_height_field), or CLEAR the
// reference when the id is 0. `constrain_movement` (0/1) sets whether the
// resolved surface constrains Motion actors (terrain-stick). Creates the
// Collision component if absent. DEFERRED (applied on the engine thread's next
// frame) and NON-BLOCKING, like the render-binding verbs; the engine re-bridges
// the reference + rebuilds the runtime scene so the constraint surface takes
// effect. Marks the scene dirty. An unknown/missing node is a logged
// engine-thread no-op. HOST-CAPABILITY GATE (require_host_scene_authoring):
// WZ_RESULT_INVALID_ARGUMENT for a null runtime, an empty node id, or a non-host
// caller. NEW exported fn — WZ_ABI_VERSION unchanged (no struct change).
WZ_ABI_API WzResult wz_host_runtime_set_node_collision(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    uint32_t asset_graph_node_id,
    uint8_t constrain_movement);

// Set node `node_id_utf8`'s Motion terrain-stick fields (issue #216/#217):
// whether the actor is constrained to the terrain surface
// (`terrain_constrained`, 0/1), its `ride_height` + `footprint_radius`, and
// whether/how strongly it aligns to the surface normal (`align_to_surface` 0/1,
// `alignment_strength`). Creates the Motion component if absent. Same deferral,
// gating, and version note as wz_host_runtime_set_node_collision; the engine
// rebuilds the runtime scene so integrate_motion + apply_terrain_constraints see
// the change.
WZ_ABI_API WzResult wz_host_runtime_set_node_motion_terrain(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    uint8_t terrain_constrained,
    float ride_height,
    float footprint_radius,
    uint8_t align_to_surface,
    float alignment_strength);

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

// Assign a per-component RENDER STYLE into node `node_id_utf8`'s GLB scene-source
// DESCRIPTOR (issue #213 Phase 3b-2 — the headline feature). The style is written
// into the persisted descriptor (base_style / a style_overrides[] entry), so a
// headless load of the saved scene.json renders the identical styled result with
// no editor: styling is DATA resolved by create_scene_from_glb, never editor-only
// state. `target_base` selects the scope:
//   1 — set the descriptor's BASE style (applied to every imported mesh unless a
//       per-mesh override wins); `mesh_index` is ignored.
//   0 — set (replace-or-insert) the per-mesh OVERRIDE for GLB `mesh_index` (the
//       mesh_index carried by each WzEditorGlbComponent in the hierarchy query).
// Only a high-impact subset of MeshRenderStyleData is authored over the ABI; the
// rest stay at engine defaults. `surface_enabled`/`wireframe_enabled` are 0/1 and
// `surface_rgba`/`wireframe_rgba` are 4-float RGBA arrays (NULL => treated as the
// default color, with the layer still toggled by the *_enabled flag). DEFERRED
// (applied on the engine thread's next frame) and NON-BLOCKING. Marks the scene
// dirty and re-materializes (the descriptor's styles fold into the Scene's content
// key, so the per-mesh renderables rebuild with the new look). An unknown/missing
// node — or one with no GLB scene source — is a logged engine-thread no-op.
//
// HOST-CAPABILITY GATE: a mutation verb gated behind the host role
// (require_host_scene_authoring). WZ_RESULT_INVALID_ARGUMENT for a null runtime,
// an empty node id, or a non-host caller.
WZ_ABI_API WzResult wz_host_runtime_set_node_glb_component_style(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    uint32_t target_base,
    uint32_t mesh_index,
    uint32_t surface_enabled,
    const float* surface_rgba,
    uint32_t wireframe_enabled,
    const float* wireframe_rgba);

// Clear the per-mesh-index render-style OVERRIDE for GLB `mesh_index` in node
// `node_id_utf8`'s GLB scene-source descriptor (issue #213 Phase 3b-2): the mesh
// falls back to the descriptor's base style. Clearing a mesh that has no override
// is a success no-op. DEFERRED + NON-BLOCKING; marks the scene dirty + re-
// materializes when something changed. An unknown/missing node, or one with no GLB
// scene source, is a logged engine-thread no-op.
//
// HOST-CAPABILITY GATE: same as above.
WZ_ABI_API WzResult wz_host_runtime_clear_node_glb_component_style(
    WzHostRuntime* runtime,
    const char* node_id_utf8,
    uint32_t mesh_index);

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

// Fetch the runtime's currently grafted scene nodes (issue #213) as a project
// snapshot blob — the SAME WzEditorProjectSnapshot layout wz_host_load_project_
// snapshot returns, so the editor decodes it with the existing reader. Only the
// `scene` part is meaningful: its roots are the instance-grafted sub-trees, each
// root carrying its host node's id as its parent (HAS_PARENT) so the editor can
// merge the sub-tree under that host in its JSON-sourced tree. The `asset_graph`
// part is ok-but-empty. These grafted nodes live only in the live runtime (an
// instanced scene_source re-imports from its reference; they are never persisted
// as authored nodes), so this is the only way the editor sees them.
//
// BLOCKING: like wz_host_runtime_add_child_node this blocks until the engine
// thread services the request on its next frame and copies its grafted nodes.
// A null or not-running runtime yields an ok blob with an EMPTY scene (never an
// error, never a crash) so the editor degrades to its JSON tree. The caller owns
// the returned buffer and frees it with wz_free_buffer.
//
// NOTE: a NEW exported function reusing the existing snapshot structs +
// project_snapshot_abi_blob — NO ABI struct change and WZ_ABI_VERSION is
// unchanged.
WZ_ABI_API WzBuffer wz_host_runtime_grafted_scene_snapshot(
    WzHostRuntime* runtime);

// A snapshot of the RUNNING scene's authored nodes, in the SAME project-snapshot
// blob layout the editor already decodes. Use it after wz_host_runtime_open_scene
// swaps the working scene (to a scenelet, or back to the main scene) to rebuild the
// editor's tree from the live scene rather than the on-disk project scene. Blocking
// (the engine thread copies its authored nodes on its next frame); a null/not-
// running runtime yields an ok blob with an EMPTY scene. Caller frees with
// wz_free_buffer. Additive; WZ_ABI_VERSION unchanged.
WZ_ABI_API WzBuffer wz_host_runtime_scene_snapshot(
    WzHostRuntime* runtime);

WZ_ABI_API void wz_free_buffer(WzBuffer* buffer);

#ifdef __cplusplus
}
#endif
