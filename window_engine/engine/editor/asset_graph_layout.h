#pragma once

#include <asset/draft.h>
#include <engine/project/project_manifest.h>

#include <string>

namespace wz::engine::editor
{
    struct AssetGraphLayoutUpdateResult
    {
        bool ok = false;
        std::string error;
    };

    AssetGraphLayoutUpdateResult update_project_asset_graph_node_layout(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        wz::asset::AssetGraphDraftNodeId node_id,
        double x,
        double y);

    AssetGraphLayoutUpdateResult update_project_asset_graph_zoom(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        double zoom);

    // Persist a draft to the project's asset-graph JSON: writes the serialized
    // graph (nodes/deps/meta) while preserving the existing file's "layout"
    // object (node positions + zoom), which is not part of the draft.
    AssetGraphLayoutUpdateResult save_project_asset_graph(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        const wz::asset::AssetGraphDraft& draft);
}
