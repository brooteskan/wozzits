#pragma once

#include <asset/compiler.h>
#include <asset/draft.h>
#include <asset/types.h>
#include <engine/editor/asset_graph_snapshot.h>
#include <engine/project/project_manifest.h>

#include <external/json/json_document.h>

#include <memory>
#include <string>

namespace wz::engine::editor
{
    enum class AssetGraphConnectionStatus : uint32_t
    {
        Compatible = 0,
        MissingNode,
        MissingCompiler,
        InvalidInputPort,
        TypeMismatch,
        SelfDependency,
        Cycle,
        DuplicateInputPort,
    };

    struct AssetGraphConnectionCheck
    {
        AssetGraphConnectionStatus status =
            AssetGraphConnectionStatus::MissingNode;
        bool compatible = false;
        bool replaces_existing = false;
        wz::asset::AssetGraphDraftNodeId from =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        wz::asset::AssetGraphDraftNodeId to =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        uint32_t to_input_port = 0;
        wz::asset::AssetType from_type = wz::asset::AssetType::Unknown;
        wz::asset::AssetType to_type = wz::asset::AssetType::Unknown;
        std::string from_type_name;
        std::string to_type_name;
        std::string message;
    };

    class AssetGraphEditorSession
    {
    public:
        AssetGraphEditorSession(
            wz::engine::project::ProjectManifestLoadDesc desc,
            wz::json::JSONDocument document,
            wz::asset::AssetGraphDraft draft,
            const wz::asset::CompilerRegistry& registry);

        AssetGraphEditorSession(const AssetGraphEditorSession&) = delete;
        AssetGraphEditorSession& operator=(const AssetGraphEditorSession&) =
            delete;

        AssetGraphEditorSession(AssetGraphEditorSession&&) noexcept = default;
        AssetGraphEditorSession& operator=(AssetGraphEditorSession&&) noexcept =
            default;

        [[nodiscard]] const wz::engine::project::ProjectManifestLoadDesc&
        desc() const noexcept
        {
            return desc_;
        }

        [[nodiscard]] bool dirty() const noexcept
        {
            return draft_.dirty;
        }

        [[nodiscard]] const wz::asset::AssetGraphDraft& draft() const noexcept
        {
            return draft_;
        }

        [[nodiscard]] wz::asset::AssetGraphDraft& draft() noexcept
        {
            return draft_;
        }

        [[nodiscard]] AssetGraphSnapshot snapshot() const;

        [[nodiscard]] AssetGraphConnectionCheck can_connect(
            wz::asset::AssetGraphDraftNodeId from,
            wz::asset::AssetGraphDraftNodeId to,
            uint32_t to_input_port) const;

        [[nodiscard]] AssetGraphConnectionCheck connect(
            wz::asset::AssetGraphDraftNodeId from,
            wz::asset::AssetGraphDraftNodeId to,
            uint32_t to_input_port);

        [[nodiscard]] bool disconnect_edge(
            wz::asset::AssetGraphDraftEdgeId edge_id);

        [[nodiscard]] bool disconnect_input(
            wz::asset::AssetGraphDraftNodeId node,
            uint32_t input_port);

        // Add a new authored source node for (schema, type) with compiler
        // default params. Returns the new draft node id, or
        // INVALID_ASSET_GRAPH_DRAFT_NODE on failure. Layout position is the
        // editor's concern (set via set_node_position after the snapshot).
        [[nodiscard]] wz::asset::AssetGraphDraftNodeId add_node(
            wz::asset::SchemaID schema,
            wz::asset::AssetType type);

        // Remove a node (and any edges touching it) from the draft. Returns
        // false if the node does not exist or was already deleted.
        [[nodiscard]] bool remove_node(
            wz::asset::AssetGraphDraftNodeId node_id);

        // Update node position / zoom in the session's in-memory layout (read by
        // snapshot(), persisted by save()). Works for not-yet-saved nodes since
        // it validates against the live draft, not the on-disk graph.
        [[nodiscard]] bool set_node_position(
            wz::asset::AssetGraphDraftNodeId node_id,
            double x,
            double y);

        [[nodiscard]] bool set_zoom(double zoom);

        // The in-memory graph document root (carries the "layout" object).
        [[nodiscard]] const wz::json::JSONValue* document_root() const noexcept
        {
            return document_.root.get();
        }

    private:
        wz::engine::project::ProjectManifestLoadDesc desc_;
        wz::json::JSONDocument document_;
        wz::asset::AssetGraphDraft draft_;
        const wz::asset::CompilerRegistry* registry_ = nullptr;
    };

    struct AssetGraphEditorSessionOpenResult
    {
        bool ok = false;
        std::string error;
        std::unique_ptr<AssetGraphEditorSession> session;
    };

    AssetGraphEditorSessionOpenResult open_asset_graph_editor_session(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        const wz::asset::CompilerRegistry& registry);
}
