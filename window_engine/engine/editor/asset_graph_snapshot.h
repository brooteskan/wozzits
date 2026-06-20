#pragma once

#include <asset/draft.h>
#include <asset/types.h>
#include <engine/project/project_manifest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::editor
{
    struct AssetGraphSnapshotPort
    {
        uint32_t index = 0;
        std::string label;
        std::string type_name;
    };

    struct AssetGraphSnapshotNode
    {
        wz::asset::AssetGraphDraftNodeId id =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        wz::asset::AssetType type = wz::asset::AssetType::Unknown;
        std::string type_name;
        wz::asset::SchemaID schema;
        std::string schema_label;
        std::string display_name;
        std::string compile_status;
        double x = 0.0;
        double y = 0.0;
        std::vector<AssetGraphSnapshotPort> input_ports;
        std::vector<AssetGraphSnapshotPort> output_ports;
    };

    struct AssetGraphSnapshotEdge
    {
        wz::asset::AssetGraphDraftNodeId from =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        wz::asset::AssetGraphDraftNodeId to =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        uint32_t to_input_port = 0;
    };

    struct AssetGraphSnapshot
    {
        std::string schema;
        double zoom = 1.0;
        std::vector<AssetGraphSnapshotNode> nodes;
        std::vector<AssetGraphSnapshotEdge> edges;
    };

    struct AssetGraphSnapshotLoadResult
    {
        bool ok = false;
        AssetGraphSnapshot snapshot;
        std::string error;
    };

    AssetGraphSnapshotLoadResult load_project_asset_graph_snapshot(
        const wz::engine::project::ProjectManifestLoadDesc& desc);
}
