#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WZ_ABI_VERSION 8u

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
    uint32_t reserved;
    WzEditorStringSpan label;
} WzEditorAssetGraphPort;

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
} WzEditorAssetGraphNode;

typedef struct WzEditorAssetGraphEdge
{
    uint64_t from;
    uint64_t to;
    uint32_t to_input_port;
    uint32_t reserved;
} WzEditorAssetGraphEdge;

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

typedef struct WzEditorSceneComponent
{
    WzEditorStringSpan kind;
    WzEditorStringSpan display_name;
} WzEditorSceneComponent;

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
    WzEditorTableSpan children;
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

#ifdef __cplusplus
static_assert(sizeof(WzEditorStringSpan) == 16);
static_assert(offsetof(WzEditorStringSpan, offset) == 0);
static_assert(offsetof(WzEditorStringSpan, size) == 8);

static_assert(sizeof(WzEditorTableSpan) == 16);
static_assert(offsetof(WzEditorTableSpan, offset) == 0);
static_assert(offsetof(WzEditorTableSpan, count) == 8);

static_assert(sizeof(WzEditorAssetGraphPort) == 24);
static_assert(offsetof(WzEditorAssetGraphPort, index) == 0);
static_assert(offsetof(WzEditorAssetGraphPort, label) == 8);

static_assert(sizeof(WzEditorAssetGraphNode) == 128);
static_assert(offsetof(WzEditorAssetGraphNode, id) == 0);
static_assert(offsetof(WzEditorAssetGraphNode, type) == 8);
static_assert(offsetof(WzEditorAssetGraphNode, type_name) == 16);
static_assert(offsetof(WzEditorAssetGraphNode, x) == 80);
static_assert(offsetof(WzEditorAssetGraphNode, input_ports) == 96);
static_assert(offsetof(WzEditorAssetGraphNode, output_ports) == 112);

static_assert(sizeof(WzEditorAssetGraphEdge) == 24);
static_assert(offsetof(WzEditorAssetGraphEdge, from) == 0);
static_assert(offsetof(WzEditorAssetGraphEdge, to) == 8);
static_assert(offsetof(WzEditorAssetGraphEdge, to_input_port) == 16);

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

static_assert(sizeof(WzEditorSceneComponent) == 32);
static_assert(offsetof(WzEditorSceneComponent, kind) == 0);
static_assert(offsetof(WzEditorSceneComponent, display_name) == 16);

static_assert(sizeof(WzEditorSceneNode) == 432);
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
static_assert(offsetof(WzEditorSceneNode, children) == 416);

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
#endif

WZ_ABI_API uint32_t wz_abi_version(void);

WZ_ABI_API WzResult wz_editor_load_project_snapshot(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    WzBuffer* out_snapshot);

WZ_ABI_API WzResult wz_editor_create_project(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    const char* name_utf8,
    WzBuffer* out_project);

WZ_ABI_API WzResult wz_editor_scene_set_node_properties(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    const char* node_id_utf8,
    const char* name_utf8,
    uint32_t visible);

WZ_ABI_API WzResult wz_editor_scene_set_node_transform(
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

WZ_ABI_API WzResult wz_editor_scene_set_camera(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    const char* node_id_utf8,
    const char* fov_y_utf8,
    const char* near_plane_utf8,
    const char* far_plane_utf8,
    const char* aspect_utf8);

WZ_ABI_API WzResult wz_editor_asset_graph_set_node_position(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    uint64_t node_id,
    double x,
    double y);

WZ_ABI_API WzResult wz_editor_asset_graph_set_zoom(
    const char* project_root_utf8,
    const char* resource_root_utf8,
    double zoom);

WZ_ABI_API void wz_free_buffer(WzBuffer* buffer);

#ifdef __cplusplus
}
#endif
