#include <engine/editor/asset_graph_editor_session.h>

#include <engine/assets/authoring/asset_graph_authoring.h>
#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/inochi/puppet_authoring.h>
#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/type_extensions.h>
#include <engine/editor/asset_graph_layout.h>

#include <logging/logger.h>

#include <cmath>

#include <external/json/json_parser.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

namespace wz::engine::editor
{
    namespace
    {
        std::string read_text_file(const wz::fs::Path& path)
        {
            // Through wz::fs so a non-ASCII path reads correctly (a narrow
            // std::ifstream(path) would misread it on Windows). {} on failure.
            const wz::fs::FileResult<std::string> result =
                wz::fs::read_file_text(path);
            return result ? result.value : std::string{};
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

        // An Any-typed port (issue #228) accepts every provider type; the
        // port's compiler validates the source kind at compile time instead.
        if (port.type != wz::asset::AssetType::Any
            && from_node->node.type != port.type)
        {
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

    wz::asset::AssetGraphDraftNodeId
    AssetGraphEditorSession::add_inochi_shared_assets()
    {
        if (!registry_) {
            return wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        }

        // The absolute project directory — the resource root the runtime's
        // asset library resolves file carriers against, so the staged puppet
        // shaders and the ShaderSource node paths agree with compile-time
        // resolution. project_root may be resource-root-relative (mirrors the
        // resolver open_asset_graph_editor_session uses).
        wz::fs::Path project_root = desc_.project_root;
        if (!wz::fs::is_absolute(project_root)
            && !desc_.resource_root.empty())
        {
            project_root = wz::fs::join(desc_.resource_root, project_root);
        }
        wz::Logger logger;
        const wz::engine::assets::authoring::GraphAuthoringContext ctx{
            .registry = *registry_,
            .resolve_file =
                [project_root](const wz::fs::Path& path) {
                    return wz::engine::assets::resolve_file_carrier_path(
                        project_root, path);
                },
        };
        return wz::engine::assets::inochi::ensure_shared_puppet_program_node(
            draft_, ctx, logger);
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

    namespace
    {
        std::array<float, 3> parse_float3(std::string_view text)
        {
            std::array<float, 3> out{ 0.0f, 0.0f, 0.0f };
            size_t index = 0;
            size_t pos = 0;
            while (index < 3u && pos < text.size()) {
                while (pos < text.size()
                    && (text[pos] == ',' || text[pos] == ' '
                        || text[pos] == '\t')) {
                    ++pos;
                }
                if (pos >= text.size()) {
                    break;
                }
                size_t end = pos;
                while (end < text.size() && text[end] != ','
                    && text[end] != ' ' && text[end] != '\t') {
                    ++end;
                }
                {
                    // Reject a non-finite strtod result ("nan"/overflow): a NaN
                    // stored here round-trips to JSON null and makes the whole
                    // graph unloadable. (C1-C55, #314)
                    const float f = static_cast<float>(
                        std::strtod(
                            std::string(text.substr(pos, end - pos)).c_str(),
                            nullptr));
                    out[index] = std::isfinite(f) ? f : 0.0f;
                }
                ++index;
                pos = end;
            }
            return out;
        }

        wz::asset::ParamValue convert_param_value(
            wz::asset::ParamType type,
            std::string_view value,
            const wz::asset::ParamDecl* decl)
        {
            const std::string text(value);
            switch (type) {
            case wz::asset::ParamType::Bool:
                return text == "true" || text == "1" || text == "True";
            case wz::asset::ParamType::Int:
                return static_cast<int64_t>(std::strtoll(
                    text.c_str(), nullptr, 10));
            case wz::asset::ParamType::Float: {
                // A "nan"/inf authored string must not be stored -- it round-trips
                // to JSON null and makes the graph unloadable. Keep a finite value;
                // otherwise fall back to the decl default. (C1-C55, #314)
                const double d = std::strtod(text.c_str(), nullptr);
                if (std::isfinite(d)) {
                    return d;
                }
                return decl != nullptr ? decl->default_num : 0.0;
            }
            case wz::asset::ParamType::Float3:
            case wz::asset::ParamType::Color:
                return parse_float3(value);
            case wz::asset::ParamType::String:
            case wz::asset::ParamType::FilePath:
                return text;
            case wz::asset::ParamType::Enum:
                if (decl != nullptr) {
                    for (size_t i = 0; i < decl->options.size(); ++i) {
                        if (decl->options[i] == value) {
                            return static_cast<int64_t>(i);
                        }
                    }
                }
                return static_cast<int64_t>(std::strtoll(
                    text.c_str(), nullptr, 10));
            }
            return text;
        }
    }

    bool AssetGraphEditorSession::set_node_param(
        wz::asset::AssetGraphDraftNodeId node_id,
        std::string_view name,
        std::string_view value)
    {
        if (name.empty()) {
            return false;
        }
        const wz::asset::AssetGraphDraftNode* node =
            wz::asset::find_asset_graph_draft_node(draft_, node_id);
        if (!node
            || node->state == wz::asset::AssetGraphDraftNodeState::Deleted) {
            return false;
        }

        wz::asset::ParamType type = wz::asset::ParamType::String;
        const wz::asset::ParamDecl* decl = nullptr;
        if (registry_ != nullptr) {
            if (const wz::asset::AssetCompiler* compiler =
                    registry_->find(node->node.schema, node->node.type)) {
                for (const wz::asset::ParamDecl& candidate :
                     compiler->parameters) {
                    if (candidate.name == name) {
                        decl = &candidate;
                        type = candidate.type;
                        break;
                    }
                }
            }
        }

        wz::asset::ParamBlock params;
        params.values[std::string(name)] =
            convert_param_value(type, value, decl);
        return wz::asset::set_asset_graph_draft_node_params(
            draft_,
            node_id,
            std::move(params));
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
