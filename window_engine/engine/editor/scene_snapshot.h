#pragma once

#include <asset/draft.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/project/project_manifest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wz::engine::editor
{
    struct SceneSnapshotTransformDisplay
    {
        std::string translation_x;
        std::string translation_y;
        std::string translation_z;
        std::string rotation_x;
        std::string rotation_y;
        std::string rotation_z;
        std::string scale_x;
        std::string scale_y;
        std::string scale_z;
    };

    struct SceneSnapshotTransform
    {
        std::vector<double> translation;
        std::vector<double> rotation_quat;
        std::vector<double> rotation_euler_degrees;
        std::vector<double> scale;
        SceneSnapshotTransformDisplay display;
    };

    struct SceneSnapshotCamera
    {
        std::optional<double> fov_y;
        std::optional<double> near_plane;
        std::optional<double> far_plane;
        std::optional<double> aspect;
    };

    struct SceneSnapshotRenderableSource
    {
        std::string kind;
        std::string display_name;
    };

    struct SceneSnapshotRenderable
    {
        std::optional<wz::asset::AssetGraphDraftNodeId> asset_graph_node_id;
        SceneSnapshotRenderableSource source;
    };

    struct SceneSnapshotComponent
    {
        std::string kind;
        std::string display_name;
    };

    // Authored Collision-component field values surfaced read-back so the editor
    // inspector restores them on select + after reload (the read-back gap fix;
    // previously collision was presence-only). collision_asset_node_id is the
    // authored asset-graph collision node id (unset when absent).
    struct SceneSnapshotCollision
    {
        std::optional<wz::asset::AssetGraphDraftNodeId> collision_asset_node_id;
        bool constrain_movement = false;
    };

    // Authored Motion-component field values surfaced read-back so the editor
    // inspector restores them (the terrain-stick subset).
    struct SceneSnapshotMotion
    {
        bool terrain_constrained = false;
        float ride_height = 0.0f;
        float footprint_radius = 0.0f;
        bool align_to_surface = false;
        float alignment_strength = 1.0f;
    };

    // Authored AudioSource-component field values surfaced read-back so the editor
    // inspector restores them on select + after reload. audio_renderable_node_id
    // is the authored audio-renderable asset-graph node id (unset when absent).
    struct SceneSnapshotAudioSource
    {
        std::optional<wz::asset::AssetGraphDraftNodeId> audio_renderable_node_id;
        bool auto_play = true;
        bool enabled = true;
    };

    // The high-impact subset of a MeshRenderStyleData that the editor reads back
    // and re-authors for a GLB component (issue #213 Phase 3b-2): surface +
    // wireframe enabled flags and RGBA colors. The rest of MeshRenderStyleData is
    // not surfaced (it stays at engine defaults; not editor-authorable here).
    struct SceneSnapshotMeshStyle
    {
        bool surface_enabled = false;
        float surface_rgba[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        bool wireframe_enabled = false;
        float wireframe_rgba[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    // One per-mesh-index style override read back from a GLB scene-source
    // descriptor's style_overrides[] (issue #213 Phase 3b-2).
    struct SceneSnapshotMeshStyleOverride
    {
        uint32_t mesh_index = 0;
        SceneSnapshotMeshStyle style;
    };

    // Summary of a node's GLB scene-source descriptor (SceneGLBSceneSource),
    // surfaced read-only so the editor can show that the node sources a GLB and
    // (Phase 3b-2) pre-fill the per-component style editor from the existing
    // assignments. Carries the summary fields plus the base style (when
    // has_base_style) and the full per-mesh override table — the subset the editor
    // can re-author.
    struct SceneSnapshotSceneSource
    {
        std::string kind;          // "glb" (only kind for now)
        std::string path;          // GLB file path
        std::string consume_mode;  // "instance" | "flatten"
        uint32_t scene_index = 0;
        uint32_t style_override_count = 0;
        bool has_base_style = false;
        SceneSnapshotMeshStyle base_style;  // meaningful iff has_base_style
        std::vector<SceneSnapshotMeshStyleOverride> style_overrides;
    };

    // One authored semantic resource binding of a node's custom-renderable
    // ingredients (issue #229/#230), surfaced read-back so the inspector
    // pre-fills its binding rows. Only the authored intent is carried
    // (semantic + asset-graph anchor); the resolved key re-bridges on bind.
    struct SceneSnapshotRenderableBinding
    {
        std::string semantic;
        std::optional<wz::asset::AssetGraphDraftNodeId> asset_graph_node_id;
    };

    // One per-instance constant override of a node's custom-renderable
    // ingredients (issue #229/#230), surfaced read-back. A field narrower
    // than 4 floats consumes a prefix of `value`.
    struct SceneSnapshotRenderableConstant
    {
        std::string name;
        float value[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    // A single config entry of an authored behavior binding, mirroring the
    // asset-graph param shape: a name, a kind ("bool"/"int"/"float"/"string")
    // and a display value string, so the editor renders it uniformly.
    struct SceneSnapshotBehaviorConfig
    {
        std::string name;
        std::string kind;
        std::string value;
    };

    struct SceneSnapshotBehavior
    {
        std::string id;
        std::string label;
        std::string module;
        std::string name;
        bool enabled = true;
        std::vector<std::string> events;
        std::vector<SceneSnapshotBehaviorConfig> config;
    };

    struct SceneSnapshotNode
    {
        std::string id;
        std::string display_name;
        std::optional<std::string> parent_id;
        std::string kind;
        std::optional<bool> visible;
        SceneSnapshotRenderableSource renderable_source;
        std::optional<SceneSnapshotTransform> transform;
        std::optional<SceneSnapshotCamera> camera;
        std::optional<SceneSnapshotRenderable> renderable;
        std::vector<SceneSnapshotComponent> components;
        std::vector<SceneSnapshotBehavior> behaviors;
        std::optional<SceneSnapshotSceneSource> scene_source;
        // Authored render-binding refs (issue #213) surfaced read-only so the
        // editor's inspector reveals + pre-selects these sections from the node's
        // persisted state (NOT a session toggle): the "Subtree from asset" node-ref
        // (scene_source "asset_graph_node_id"), and the render-binding geometry +
        // render-program node ids. Each is the authored asset-graph node id; the
        // resolved keys are re-bridged on bind, so only the id is surfaced.
        std::optional<wz::asset::AssetGraphDraftNodeId> scene_source_node_id;
        std::optional<wz::asset::AssetGraphDraftNodeId> geometry_node_id;
        std::optional<wz::asset::AssetGraphDraftNodeId> render_program_node_id;
        // Persisted Collision/Motion component field values (read-back gap fix),
        // surfaced so the inspector restores them on select + after reload.
        std::optional<SceneSnapshotCollision> collision;
        std::optional<SceneSnapshotMotion> motion;
        std::optional<SceneSnapshotAudioSource> audio_source;
        // Custom-renderable ingredients (issue #229/#230), surfaced read-back
        // (possibly empty) so the inspector pre-fills its binding rows +
        // constant overrides from the node's persisted state.
        std::vector<SceneSnapshotRenderableBinding> renderable_bindings;
        std::vector<SceneSnapshotRenderableConstant> renderable_constants;
        std::vector<SceneSnapshotNode> children;
    };

    struct SceneSnapshot
    {
        std::string schema;
        std::string name;
        std::vector<SceneSnapshotNode> roots;
    };

    struct SceneSnapshotLoadResult
    {
        bool ok = false;
        SceneSnapshot snapshot;
        std::string error;
    };

    SceneSnapshotLoadResult load_project_scene_snapshot(
        const wz::engine::project::ProjectManifestLoadDesc& desc);

    // Build a SceneSnapshot directly from live SceneNodeAsset entries (issue
    // #213), rather than from a scene.json file. Used to surface the runtime-only
    // grafted scene nodes (WozzitsApp_v1::grafted_scene_nodes) so the editor can
    // merge them under their hosts. Each SceneNodeAsset maps to a SceneSnapshotNode
    // (id; display_name=name; parent_id; kind; visible; local transform; renderable
    // presence). Parent/child is assembled like the file path: a node whose parent
    // is not in `nodes` becomes a root but KEEPS its parent_id (that is how the
    // editor finds the host to graft under). Grafted nodes are plain — no
    // camera/components/behaviors are surfaced (they are not authored/editable).
    SceneSnapshot build_scene_snapshot_from_nodes(
        const std::vector<wz::engine::assets::SceneNodeAsset>& nodes);
}
