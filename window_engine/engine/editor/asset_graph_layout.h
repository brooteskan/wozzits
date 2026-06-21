#pragma once

#include <asset/draft.h>
#include <engine/project/project_manifest.h>

#include <string>

namespace wz::json
{
    struct JSONValue;
}

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

    // In-memory layout edits on a graph-root JSONValue's "layout" object (no
    // disk). Shared by the disk-based project paths above and the in-process
    // editor session, so a freshly added (unsaved) node can be positioned.
    void apply_asset_graph_layout_node(
        wz::json::JSONValue& graph_root,
        wz::asset::AssetGraphDraftNodeId node_id,
        double x,
        double y);

    void apply_asset_graph_layout_zoom(
        wz::json::JSONValue& graph_root,
        double zoom);

    // Persist a draft to the project's asset-graph JSON: writes the serialized
    // graph (nodes/deps/meta) while preserving a "layout" object (node positions
    // + zoom), which is not part of the draft. When layout_source is non-null its
    // "layout" is copied (the in-memory session layout); otherwise the existing
    // on-disk file's "layout" is preserved.
    AssetGraphLayoutUpdateResult save_project_asset_graph(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        const wz::asset::AssetGraphDraft& draft,
        const wz::json::JSONValue* layout_source = nullptr);
}
