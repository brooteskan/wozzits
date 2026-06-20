#pragma once

#include <engine/editor/asset_graph_snapshot.h>
#include <engine/editor/scene_snapshot.h>
#include <engine/project/project_manifest.h>

#include <string>

namespace wz::engine::editor
{
    struct ProjectSnapshotLoadResult
    {
        bool ok = false;
        wz::engine::project::ProjectManifestProbeStatus status =
            wz::engine::project::ProjectManifestProbeStatus::Invalid;
        std::string error;
        std::string project_name;
        AssetGraphSnapshotLoadResult asset_graph;
        SceneSnapshotLoadResult scene;
    };

    ProjectSnapshotLoadResult load_project_snapshot(
        const wz::engine::project::ProjectManifestLoadDesc& desc);
}
