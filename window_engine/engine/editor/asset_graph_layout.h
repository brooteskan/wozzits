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
}
