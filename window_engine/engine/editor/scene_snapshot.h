#pragma once

#include <asset/draft.h>
#include <engine/project/project_manifest.h>

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
}
