#pragma once

#include <asset/draft.h>
#include <asset/types.h>
#include <engine/project/project_manifest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::json
{
    struct JSONValue;
}

namespace wz::engine::editor
{
    struct AssetGraphSnapshotPort
    {
        uint32_t index = 0;
        wz::asset::AssetType type = wz::asset::AssetType::Unknown;
        std::string name;
        std::string label;
        std::string type_name;
        bool required = true;
        bool many = false;
    };

    struct AssetGraphSnapshotParam
    {
        std::string name;
        // Declared widget type from the compiler's ParamDecl (authoritative):
        // "bool" | "int" | "float" | "float3" | "color" | "string" |
        // "filepath" | "enum". Falls back to the stored-variant kind when no
        // compiler/decl is available.
        std::string kind;
        std::string value;  // display text; for "enum", the selected option name
        std::vector<std::string> options;  // enum choices ("enum" only)
    };

    struct AssetGraphSnapshotDiagnostic
    {
        wz::asset::AssetGraphDraftValidationSeverity severity =
            wz::asset::AssetGraphDraftValidationSeverity::Info;
        wz::asset::AssetGraphDraftValidationCode code =
            wz::asset::AssetGraphDraftValidationCode::None;
        wz::asset::AssetGraphDraftNodeId node =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        wz::asset::AssetGraphDraftEdgeId edge =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_EDGE;
        uint32_t input_port = 0;
        std::string message;
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
        std::vector<AssetGraphSnapshotDiagnostic> diagnostics;
        std::vector<AssetGraphSnapshotParam> params;
    };

    struct AssetGraphSnapshotEdge
    {
        wz::asset::AssetGraphDraftEdgeId id =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_EDGE;
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

    AssetGraphSnapshot build_asset_graph_snapshot(
        const wz::json::JSONValue& root,
        const wz::asset::AssetGraphDraft& draft,
        const wz::asset::CompilerRegistry* registry = nullptr);

    AssetGraphSnapshotLoadResult load_project_asset_graph_snapshot(
        const wz::engine::project::ProjectManifestLoadDesc& desc);

    AssetGraphSnapshotLoadResult load_project_asset_graph_snapshot(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        const wz::asset::CompilerRegistry& registry);
}
