#include <engine/editor/asset_graph_editor_session.h>

#include <engine/assets/authoring/asset_graph_authoring.h>
#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/type_extensions.h>
#include <engine/editor/asset_graph_layout.h>

#include <cmath>

#include <external/json/json_parser.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace wz::engine::editor
{
    namespace
    {
        std::string read_text_file(const wz::fs::Path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                return {};
            }

            return std::string(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
        }

        wz::fs::Path resolve_resource_path(
            const wz::fs::Path& resource_root,
            const wz::fs::Path& path)
        {
            return wz::fs::is_absolute(path) || resource_root.empty()
                ? path
                : wz::fs::join(resource_root, path);
        }

        std::string type_name(wz::asset::AssetType type)
        {
            return std::string(
                wz::engine::assets::asset_type_display_name_view(type));
        }

        AssetGraphConnectionCheck connection_check(
            AssetGraphConnectionStatus status,
            wz::asset::AssetGraphDraftNodeId from,
            wz::asset::AssetGraphDraftNodeId to,
            uint32_t to_input_port,
            std::string message)
        {
            return AssetGraphConnectionCheck{
                .status = status,
                .compatible = status == AssetGraphConnectionStatus::Compatible,
                .from = from,
                .to = to,
                .to_input_port = to_input_port,
                .message = std::move(message),
            };
        }

        AssetGraphConnectionStatus status_for_validation_code(
            wz::asset::AssetGraphDraftValidationCode code)
        {
            switch (code) {
            case wz::asset::AssetGraphDraftValidationCode::InvalidInputPort:
                return AssetGraphConnectionStatus::InvalidInputPort;
            case wz::asset::AssetGraphDraftValidationCode::TypeMismatch:
                return AssetGraphConnectionStatus::TypeMismatch;
            case wz::asset::AssetGraphDraftValidationCode::MissingNode:
                return AssetGraphConnectionStatus::MissingNode;
            case wz::asset::AssetGraphDraftValidationCode::SelfDependency:
                return AssetGraphConnectionStatus::SelfDependency;
            case wz::asset::AssetGraphDraftValidationCode::Cycle:
                return AssetGraphConnectionStatus::Cycle;
            case wz::asset::AssetGraphDraftValidationCode::DuplicateInputPort:
                return AssetGraphConnectionStatus::DuplicateInputPort;
            case wz::asset::AssetGraphDraftValidationCode::UnknownCompiler:
                return AssetGraphConnectionStatus::MissingCompiler;
            default:
                return AssetGraphConnectionStatus::TypeMismatch;
            }
        }

        bool has_existing_input_edge(
            const wz::asset::AssetGraphDraft& draft,
            wz::asset::AssetGraphDraftNodeId node,
            uint32_t input_port)
        {
            return std::any_of(
                draft.edges.begin(),
                draft.edges.end(),
                [node, input_port](const wz::asset::AssetGraphDraftEdge& edge)
                {
                    return edge.to == node
                        && edge.to_input_port == input_port;
                });
        }

        const wz::asset::AssetGraphDraftValidationMessage*
        candidate_edge_error(
            const wz::asset::AssetGraphDraft& draft,
            wz::asset::AssetGraphDraftEdgeId edge_id,
            wz::asset::AssetGraphDraftNodeId to,
            uint32_t input_port)
        {
            for (const wz::asset::AssetGraphDraftValidationMessage& message :
                 draft.validation_messages)
            {
                if (message.severity
                    != wz::asset::AssetGraphDraftValidationSeverity::Error)
                {
                    continue;
                }

                if (message.edge == edge_id
                    || message.code
                        == wz::asset::AssetGraphDraftValidationCode::Cycle
                    || (message.code
                            == wz::asset::AssetGraphDraftValidationCode::
                                DuplicateInputPort
                        && message.node == to
                        && message.input_port == input_port))
                {
                    return &message;
                }
            }

            return nullptr;
        }

        void invalidate_node_inputs(
            wz::asset::AssetGraphDraft& draft,
            wz::asset::AssetGraphDraftNodeId node_id)
        {
            if (wz::asset::AssetGraphDraftNode* node =
                    wz::asset::find_asset_graph_draft_node(draft, node_id))
            {
                wz::asset::invalidate_asset_graph_draft_node_key(*node);
                wz::asset::mark_asset_graph_draft_node_modified(*node);
                wz::asset::rebuild_asset_graph_draft_indexes(draft);
            }
        }
    }

    AssetGraphEditorSession::AssetGraphEditorSession(
        wz::engine::project::ProjectManifestLoadDesc desc,
        wz::json::JSONDocument document,
        wz::asset::AssetGraphDraft draft,
        const wz::asset::CompilerRegistry& registry)
        : desc_(std::move(desc))
        , document_(std::move(document))
        , draft_(std::move(draft))
        , registry_(&registry)
    {
        wz::asset::rebuild_asset_graph_draft_indexes(draft_);
    }

    AssetGraphSnapshot AssetGraphEditorSession::snapshot() const
    {
        return build_asset_graph_snapshot(
            *document_.root,
            draft_,
            registry_);
    }

    AssetGraphConnectionCheck AssetGraphEditorSession::can_connect(
        wz::asset::AssetGraphDraftNodeId from,
        wz::asset::AssetGraphDraftNodeId to,
        uint32_t to_input_port) const
    {
        const wz::asset::AssetGraphDraftNode* from_node =
            wz::asset::find_asset_graph_draft_node(draft_, from);
        const wz::asset::AssetGraphDraftNode* to_node =
            wz::asset::find_asset_graph_draft_node(draft_, to);
        if (!from_node || !to_node
            || from_node->state == wz::asset::AssetGraphDraftNodeState::Deleted
            || to_node->state == wz::asset::AssetGraphDraftNodeState::Deleted)
        {
            return connection_check(
                AssetGraphConnectionStatus::MissingNode,
                from,
                to,
                to_input_port,
                "edge endpoint is missing");
        }

        if (from == to) {
            return connection_check(
                AssetGraphConnectionStatus::SelfDependency,
                from,
                to,
                to_input_port,
                "edge connects a node to itself");
        }

        const wz::asset::AssetCompiler* compiler =
            registry_->find(to_node->node.schema, to_node->node.type);
        if (!compiler) {
            return connection_check(
                AssetGraphConnectionStatus::MissingCompiler,
                from,
                to,
                to_input_port,
                "target node has no registered compiler");
        }

        if (to_input_port >= compiler->input_ports.size()) {
            return connection_check(
                AssetGraphConnectionStatus::InvalidInputPort,
                from,
                to,
                to_input_port,
                "edge targets an input port outside the compiler schema");
        }

        const wz::asset::InputPort& port =
            compiler->input_ports[to_input_port];
        AssetGraphConnectionCheck check{};
        check.status = AssetGraphConnectionStatus::Compatible;
        check.compatible = true;
        check.replaces_existing =
            !wz::asset::input_port_many(port)
            && has_existing_input_edge(draft_, to, to_input_port);
        check.from = from;
        check.to = to;
        check.to_input_port = to_input_port;
        check.from_type = from_node->node.type;
        check.to_type = port.type;
        check.from_type_name = type_name(from_node->node.type);
        check.to_type_name = type_name(port.type);

        if (from_node->node.type != port.type) {
            check.status = AssetGraphConnectionStatus::TypeMismatch;
            check.compatible = false;
            check.message =
                "edge provider type does not match the input port type";
        }

        return check;
    }

    AssetGraphConnectionCheck AssetGraphEditorSession::connect(
        wz::asset::AssetGraphDraftNodeId from,
        wz::asset::AssetGraphDraftNodeId to,
        uint32_t to_input_port)
    {
        AssetGraphConnectionCheck check =
            can_connect(from, to, to_input_port);
        if (!check.compatible) {
            return check;
        }

        const wz::asset::AssetGraphDraftNode* to_node =
            wz::asset::find_asset_graph_draft_node(draft_, to);
        const wz::asset::AssetCompiler* compiler =
            to_node ? registry_->find(to_node->node.schema, to_node->node.type)
                    : nullptr;
        const bool many =
            compiler
            && to_input_port < compiler->input_ports.size()
            && wz::asset::input_port_many(compiler->input_ports[to_input_port]);

        std::vector<wz::asset::AssetGraphDraftEdge> previous_edges =
            draft_.edges;
        const wz::asset::AssetGraphDraftEdgeId previous_next_edge_id =
            draft_.next_edge_id;
        const bool previous_dirty = draft_.dirty;

        if (!many) {
            wz::asset::disconnect_asset_graph_draft_input(
                draft_,
                to,
                to_input_port);
        }

        const wz::asset::AssetGraphDraftEdgeId edge_id =
            wz::asset::connect_asset_graph_draft_nodes(
                draft_,
                from,
                to,
                to_input_port);
        if (edge_id == wz::asset::INVALID_ASSET_GRAPH_DRAFT_EDGE) {
            draft_.edges = std::move(previous_edges);
            draft_.next_edge_id = previous_next_edge_id;
            draft_.dirty = previous_dirty;
            wz::asset::rebuild_asset_graph_draft_indexes(draft_);
            check.status = AssetGraphConnectionStatus::MissingNode;
            check.compatible = false;
            check.message = "edge endpoint is missing";
            return check;
        }

        (void)wz::asset::validate_asset_graph_draft(
            draft_,
            *registry_,
            true);
        if (const wz::asset::AssetGraphDraftValidationMessage* message =
                candidate_edge_error(draft_, edge_id, to, to_input_port))
        {
            check.status = status_for_validation_code(message->code);
            check.compatible = false;
            check.message = message->message;
            draft_.edges = std::move(previous_edges);
            draft_.next_edge_id = previous_next_edge_id;
            draft_.dirty = previous_dirty;
            draft_.validation_messages.clear();
            wz::asset::rebuild_asset_graph_draft_indexes(draft_);
            return check;
        }

        draft_.dirty = true;
        invalidate_node_inputs(draft_, to);
        check.compatible = true;
        return check;
    }

    bool AssetGraphEditorSession::disconnect_edge(
        wz::asset::AssetGraphDraftEdgeId edge_id)
    {
        const wz::asset::AssetGraphDraftEdge* edge =
            wz::asset::find_asset_graph_draft_edge(draft_, edge_id);
        const wz::asset::AssetGraphDraftNodeId to =
            edge ? edge->to : wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;

        if (!wz::asset::disconnect_asset_graph_draft_edge(draft_, edge_id)) {
            return false;
        }

        invalidate_node_inputs(draft_, to);
        return true;
    }

    wz::asset::AssetGraphDraftNodeId AssetGraphEditorSession::add_node(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type)
    {
        if (type == wz::asset::AssetType::Unknown || !registry_) {
            return wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        }

        const wz::engine::assets::authoring::GraphAuthoringContext ctx{
            .registry = *registry_,
            .resolve_file = nullptr,
        };
        return wz::engine::assets::authoring::add_source_asset_node(
            draft_,
            ctx,
            schema,
            type);
    }

    bool AssetGraphEditorSession::remove_node(
        wz::asset::AssetGraphDraftNodeId node_id)
    {
        return wz::asset::remove_asset_graph_draft_node(draft_, node_id);
    }

    bool AssetGraphEditorSession::set_node_position(
        wz::asset::AssetGraphDraftNodeId node_id,
        double x,
        double y)
    {
        constexpr double kMaxAbsGraphCoordinate = 100000.0;
        if (node_id == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE
            || !document_.root
            || !std::isfinite(x) || !std::isfinite(y)
            || std::fabs(x) > kMaxAbsGraphCoordinate
            || std::fabs(y) > kMaxAbsGraphCoordinate)
        {
            return false;
        }
        if (!wz::asset::find_asset_graph_draft_node(draft_, node_id)) {
            return false;
        }

        apply_asset_graph_layout_node(*document_.root, node_id, x, y);
        return true;
    }

    bool AssetGraphEditorSession::set_zoom(double zoom)
    {
        constexpr double kMinGraphZoom = 0.25;
        constexpr double kMaxGraphZoom = 4.0;
        if (!document_.root || !std::isfinite(zoom)
            || zoom < kMinGraphZoom || zoom > kMaxGraphZoom)
        {
            return false;
        }

        apply_asset_graph_layout_zoom(*document_.root, zoom);
        return true;
    }

    bool AssetGraphEditorSession::disconnect_input(
        wz::asset::AssetGraphDraftNodeId node,
        uint32_t input_port)
    {
        if (!wz::asset::disconnect_asset_graph_draft_input(
                draft_,
                node,
                input_port))
        {
            return false;
        }

        invalidate_node_inputs(draft_, node);
        return true;
    }

    AssetGraphEditorSessionOpenResult open_asset_graph_editor_session(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        const wz::asset::CompilerRegistry& registry)
    {
        AssetGraphEditorSessionOpenResult result;

        const auto loaded =
            wz::engine::project::load_project_manifest(desc);
        if (!loaded.ok) {
            result.error = loaded.error;
            return result;
        }
        if (loaded.manifest.asset_graph_path.empty()) {
            result.error = "project manifest does not define an asset graph";
            return result;
        }

        const wz::fs::Path graph_path = resolve_resource_path(
            desc.resource_root,
            loaded.manifest.asset_graph_path);
        const std::string graph_text = read_text_file(graph_path);
        if (graph_text.empty()) {
            result.error =
                "cannot read asset graph: " + loaded.manifest.asset_graph_path;
            return result;
        }

        wz::json::JSONParseResult parsed =
            wz::json::parse_json_string(graph_text);
        if (!parsed.ok || !parsed.document.root) {
            result.error = "invalid asset graph json";
            return result;
        }

        wz::asset::AssetGraphDraft draft;
        std::string error;
        if (!wz::engine::assets::load_asset_graph_draft_from_v2_json(
                *parsed.document.root,
                draft,
                error))
        {
            result.error = "asset graph parse failed: " + error;
            return result;
        }

        result.session = std::make_unique<AssetGraphEditorSession>(
            desc,
            std::move(parsed.document),
            std::move(draft),
            registry);
        result.ok = true;
        return result;
    }
}
