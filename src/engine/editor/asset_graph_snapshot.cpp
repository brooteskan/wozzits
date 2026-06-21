#include <engine/editor/asset_graph_snapshot.h>

#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/type_extensions.h>

#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace wz::engine::editor
{
    namespace
    {
        constexpr double kFallbackColumnSpacing = 260.0;
        constexpr double kFallbackRowSpacing = 150.0;
        constexpr double kMinGraphZoom = 0.25;
        constexpr double kMaxGraphZoom = 4.0;

        struct LayoutPosition
        {
            double x = 0.0;
            double y = 0.0;
        };

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

        std::string schema_tail(wz::asset::SchemaID schema)
        {
            std::ostringstream out;
            out << std::hex << std::setfill('0') << std::setw(8)
                << static_cast<uint32_t>(schema.value & 0xffffffffull);
            return out.str();
        }

        std::string type_name(wz::asset::AssetType type)
        {
            return std::string(wz::engine::assets::asset_type_display_name_view(type));
        }

        std::optional<std::string> string_param(
            const wz::asset::ParamBlock& params,
            const std::string& name)
        {
            const auto it = params.values.find(name);
            if (it == params.values.end()) {
                return std::nullopt;
            }
            if (const auto* value = std::get_if<std::string>(&it->second)) {
                return *value;
            }
            return std::nullopt;
        }

        std::string filename_view(const std::string& path)
        {
            const size_t slash = path.find_last_of("\\/");
            if (slash == std::string::npos || slash + 1u >= path.size()) {
                return path;
            }
            return path.substr(slash + 1u);
        }

        std::string display_name(const wz::asset::AssetNode& node)
        {
            if (const auto* params = std::any_cast<wz::asset::ParamBlock>(
                    &node.meta))
            {
                if (const auto name = string_param(*params, "name");
                    name && !name->empty())
                {
                    return *name;
                }
                if (const auto source_path =
                        string_param(*params, "source_path");
                    source_path && !source_path->empty())
                {
                    const std::string file_name = filename_view(*source_path);
                    if (!file_name.empty()) {
                        return file_name;
                    }
                }
            }

            return type_name(node.type);
        }

        std::string param_type_name(wz::asset::ParamType type)
        {
            switch (type) {
                case wz::asset::ParamType::Bool:     return "bool";
                case wz::asset::ParamType::Int:      return "int";
                case wz::asset::ParamType::Float:    return "float";
                case wz::asset::ParamType::Float3:   return "float3";
                case wz::asset::ParamType::Color:    return "color";
                case wz::asset::ParamType::String:   return "string";
                case wz::asset::ParamType::FilePath: return "filepath";
                case wz::asset::ParamType::Enum:     return "enum";
            }
            return "string";
        }

        // Storage-variant display text (used directly for non-enum params and
        // as the fallback when there is no compiler decl).
        std::string param_value_text(const wz::asset::ParamValue& value)
        {
            return std::visit(
                [](const auto& typed) -> std::string
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::is_same_v<T, bool>) {
                        return typed ? "true" : "false";
                    }
                    else if constexpr (std::is_same_v<T, int64_t>) {
                        return std::to_string(typed);
                    }
                    else if constexpr (std::is_same_v<T, double>) {
                        return std::to_string(typed);
                    }
                    else if constexpr (
                        std::is_same_v<T, std::array<float, 3>>)
                    {
                        return std::to_string(typed[0]) + ", "
                            + std::to_string(typed[1]) + ", "
                            + std::to_string(typed[2]);
                    }
                    else if constexpr (std::is_same_v<T, std::string>) {
                        return typed;
                    }
                    else {
                        return std::string{};
                    }
                },
                value);
        }

        // Variant-derived kind for the no-compiler fallback path.
        std::string storage_kind(const wz::asset::ParamValue& value)
        {
            return std::visit(
                [](const auto& typed) -> std::string
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::is_same_v<T, bool>) {
                        return "bool";
                    }
                    else if constexpr (std::is_same_v<T, int64_t>) {
                        return "int";
                    }
                    else if constexpr (std::is_same_v<T, double>) {
                        return "float";
                    }
                    else if constexpr (
                        std::is_same_v<T, std::array<float, 3>>)
                    {
                        return "float3";
                    }
                    else {
                        return "string";
                    }
                },
                value);
        }

        int64_t enum_index(
            const wz::asset::ParamValue& value,
            double fallback)
        {
            if (const auto* index = std::get_if<int64_t>(&value)) {
                return *index;
            }
            if (const auto* number = std::get_if<double>(&value)) {
                return static_cast<int64_t>(*number);
            }
            return static_cast<int64_t>(fallback);
        }

        // Drive params off the compiler's declared parameters so the widget
        // type (Enum, FilePath, Color, ...) and enum option names survive,
        // rather than collapsing to the stored variant. Enums in particular
        // are stored as int64 and would otherwise read as plain "int".
        std::vector<AssetGraphSnapshotParam> params_for_node(
            const wz::asset::CompilerRegistry* registry,
            const wz::asset::AssetNode& node)
        {
            std::vector<AssetGraphSnapshotParam> out;
            const auto* params =
                std::any_cast<wz::asset::ParamBlock>(&node.meta);
            const wz::asset::AssetCompiler* compiler =
                registry ? registry->find(node.schema, node.type) : nullptr;

            if (compiler && !compiler->parameters.empty()) {
                out.reserve(compiler->parameters.size());
                for (const wz::asset::ParamDecl& decl : compiler->parameters) {
                    AssetGraphSnapshotParam param;
                    param.name = std::string(decl.name);
                    param.kind = param_type_name(decl.type);
                    for (const std::string_view option : decl.options) {
                        param.options.emplace_back(option);
                    }

                    const wz::asset::ParamValue* stored = nullptr;
                    if (params) {
                        const auto it = params->values.find(param.name);
                        if (it != params->values.end()) {
                            stored = &it->second;
                        }
                    }

                    if (decl.type == wz::asset::ParamType::Enum) {
                        if (stored != nullptr) {
                            if (const auto* text =
                                    std::get_if<std::string>(stored))
                            {
                                param.value = *text;
                            }
                            else {
                                const int64_t index =
                                    enum_index(*stored, decl.default_num);
                                param.value =
                                    (index >= 0
                                     && index < static_cast<int64_t>(
                                            param.options.size()))
                                        ? param.options[
                                              static_cast<size_t>(index)]
                                        : std::to_string(index);
                            }
                        }
                        else {
                            const int64_t index =
                                static_cast<int64_t>(decl.default_num);
                            param.value =
                                (index >= 0
                                 && index < static_cast<int64_t>(
                                        param.options.size()))
                                    ? param.options[static_cast<size_t>(index)]
                                    : std::to_string(index);
                        }
                    }
                    else if (stored != nullptr) {
                        param.value = param_value_text(*stored);
                    }
                    else if (!decl.default_str.empty()) {
                        param.value = std::string(decl.default_str);
                    }
                    else if (decl.type == wz::asset::ParamType::Bool) {
                        param.value =
                            (decl.default_num != 0.0) ? "true" : "false";
                    }
                    else if (decl.type == wz::asset::ParamType::Int) {
                        param.value = std::to_string(
                            static_cast<int64_t>(decl.default_num));
                    }
                    else if (decl.type == wz::asset::ParamType::Float) {
                        param.value = std::to_string(decl.default_num);
                    }

                    out.push_back(std::move(param));
                }

                // Preserve the declared parameter order (meaningful, stable).
                return out;
            }

            if (!params) {
                return out;
            }

            // No compiler/decl: fall back to stored-variant inference.
            out.reserve(params->values.size());
            for (const auto& [name, value] : params->values) {
                AssetGraphSnapshotParam param;
                param.name = name;
                param.kind = storage_kind(value);
                param.value = param_value_text(value);
                out.push_back(std::move(param));
            }

            // Stable order so snapshots/diffs are deterministic.
            std::sort(
                out.begin(),
                out.end(),
                [](const AssetGraphSnapshotParam& a,
                   const AssetGraphSnapshotParam& b)
                {
                    return a.name < b.name;
                });
            return out;
        }

        std::unordered_map<wz::asset::AssetGraphDraftNodeId, LayoutPosition>
            read_layout(const wz::json::JSONValue& root)
        {
            std::unordered_map<
                wz::asset::AssetGraphDraftNodeId,
                LayoutPosition>
                layout;

            const auto* layout_root = wz::json::find_member(root, "layout");
            if (!layout_root
                || layout_root->kind != wz::json::JSONValueKind::Object)
            {
                return layout;
            }

            const auto* nodes = wz::json::find_member(*layout_root, "nodes");
            if (!nodes || nodes->kind != wz::json::JSONValueKind::Array) {
                return layout;
            }

            for (const auto& item : nodes->array_values) {
                if (!item || item->kind != wz::json::JSONValueKind::Object) {
                    continue;
                }
                const auto id = wz::json::read_number(*item, "node_id");
                if (!id || *id < 0.0) {
                    continue;
                }
                layout[static_cast<wz::asset::AssetGraphDraftNodeId>(*id)] =
                    LayoutPosition{
                        .x = wz::json::read_number(*item, "x").value_or(0.0),
                        .y = wz::json::read_number(*item, "y").value_or(0.0),
                    };
            }

            return layout;
        }

        double read_zoom(const wz::json::JSONValue& root)
        {
            const auto* layout_root = wz::json::find_member(root, "layout");
            if (!layout_root
                || layout_root->kind != wz::json::JSONValueKind::Object)
            {
                return 1.0;
            }

            const auto zoom = wz::json::read_number(*layout_root, "zoom");
            if (!zoom || !std::isfinite(*zoom)
                || *zoom < kMinGraphZoom || *zoom > kMaxGraphZoom)
            {
                return 1.0;
            }

            return *zoom;
        }

        LayoutPosition fallback_position(std::size_t index)
        {
            return LayoutPosition{
                .x = static_cast<double>(index % 4u) * kFallbackColumnSpacing,
                .y = static_cast<double>(index / 4u) * kFallbackRowSpacing,
            };
        }

        AssetGraphSnapshotPort port_from_decl(
            uint32_t index,
            const wz::asset::InputPort& port)
        {
            const std::string type_label = type_name(port.type);
            return AssetGraphSnapshotPort{
                .index = index,
                .type = port.type,
                .name = std::string(port.name),
                .label = type_label,
                .type_name = type_label,
                .required = wz::asset::input_port_required(port),
                .many = wz::asset::input_port_many(port),
            };
        }

        std::vector<AssetGraphSnapshotPort> declared_input_ports_for_node(
            const wz::asset::CompilerRegistry& registry,
            const wz::asset::AssetNode& node)
        {
            const wz::asset::AssetCompiler* compiler =
                registry.find(node.schema, node.type);
            if (!compiler) {
                return {};
            }

            std::vector<AssetGraphSnapshotPort> result;
            result.reserve(compiler->input_ports.size());
            for (uint32_t i = 0;
                 i < static_cast<uint32_t>(compiler->input_ports.size());
                 ++i)
            {
                result.push_back(port_from_decl(i, compiler->input_ports[i]));
            }
            return result;
        }

        std::vector<AssetGraphSnapshotPort> fallback_input_ports_for_node(
            const wz::asset::AssetGraphDraft& draft,
            wz::asset::AssetGraphDraftNodeId node_id,
            const std::unordered_map<
                wz::asset::AssetGraphDraftNodeId,
                wz::asset::AssetType>& node_types)
        {
            std::map<uint32_t, wz::asset::AssetType> ports;
            for (const wz::asset::AssetGraphDraftEdge& edge : draft.edges) {
                if (edge.to == node_id) {
                    wz::asset::AssetType type =
                        wz::asset::AssetType::Unknown;
                    if (const auto from_type = node_types.find(edge.from);
                        from_type != node_types.end())
                    {
                        type = from_type->second;
                    }
                    ports.emplace(edge.to_input_port, type);
                }
            }

            std::vector<AssetGraphSnapshotPort> result;
            result.reserve(ports.size());
            for (const auto& [port, type] : ports) {
                const std::string type_label = type_name(type);
                result.push_back(AssetGraphSnapshotPort{
                    .index = port,
                    .type = type,
                    .label = type_label,
                    .type_name = type_label,
                });
            }
            return result;
        }

        bool diagnostic_belongs_to_node(
            const wz::asset::AssetGraphDraft& draft,
            const wz::asset::AssetGraphDraftValidationMessage& message,
            wz::asset::AssetGraphDraftNodeId node_id)
        {
            if (message.node == node_id) {
                return true;
            }
            if (message.edge
                == wz::asset::INVALID_ASSET_GRAPH_DRAFT_EDGE)
            {
                return false;
            }
            const wz::asset::AssetGraphDraftEdge* edge =
                wz::asset::find_asset_graph_draft_edge(
                    draft,
                    message.edge);
            return edge && (edge->from == node_id || edge->to == node_id);
        }

        std::vector<AssetGraphSnapshotDiagnostic> diagnostics_for_node(
            const wz::asset::AssetGraphDraft& draft,
            wz::asset::AssetGraphDraftNodeId node_id)
        {
            std::vector<AssetGraphSnapshotDiagnostic> result;
            for (const wz::asset::AssetGraphDraftValidationMessage& message :
                 draft.validation_messages)
            {
                if (!diagnostic_belongs_to_node(draft, message, node_id)) {
                    continue;
                }
                result.push_back(AssetGraphSnapshotDiagnostic{
                    .severity = message.severity,
                    .code = message.code,
                    .severity_name = std::string(
                        wz::asset::asset_graph_draft_validation_severity_name(
                            message.severity)),
                    .code_name = std::string(
                        wz::asset::asset_graph_draft_validation_code_name(
                            message.code)),
                    .node = message.node,
                    .edge = message.edge,
                    .input_port = message.input_port,
                    .message = message.message,
                });
            }
            return result;
        }

        bool has_error_diagnostic(
            const std::vector<AssetGraphSnapshotDiagnostic>& diagnostics)
        {
            return std::any_of(
                diagnostics.begin(),
                diagnostics.end(),
                [](const AssetGraphSnapshotDiagnostic& diagnostic)
                {
                    return diagnostic.severity
                        == wz::asset::AssetGraphDraftValidationSeverity::Error;
                });
        }

        std::unordered_set<wz::asset::AssetGraphDraftNodeId>
        changed_nodes_for_status(const wz::asset::AssetGraphDraft& draft)
        {
            std::unordered_set<wz::asset::AssetGraphDraftNodeId> changed;
            for (const wz::asset::AssetGraphDraftNode& node : draft.nodes) {
                if (node.state == wz::asset::AssetGraphDraftNodeState::Deleted) {
                    continue;
                }
                if (!wz::asset::asset_graph_draft_key_valid(node.node.key)) {
                    changed.insert(node.id);
                }
            }

            bool added = true;
            while (added) {
                added = false;
                for (const wz::asset::AssetGraphDraftEdge& edge : draft.edges) {
                    if (changed.count(edge.from) == 0u
                        || changed.count(edge.to) != 0u)
                    {
                        continue;
                    }
                    changed.insert(edge.to);
                    added = true;
                }
            }
            return changed;
        }

        std::string compile_status_for_node(
            const std::vector<AssetGraphSnapshotDiagnostic>& diagnostics,
            const std::unordered_set<wz::asset::AssetGraphDraftNodeId>& changed,
            wz::asset::AssetGraphDraftNodeId node_id)
        {
            if (has_error_diagnostic(diagnostics)) {
                return "error";
            }
            if (changed.count(node_id) != 0u) {
                return "changed";
            }
            return "ready";
        }
    }

    AssetGraphSnapshot build_asset_graph_snapshot(
        const wz::json::JSONValue& root,
        const wz::asset::AssetGraphDraft& draft,
        const wz::asset::CompilerRegistry* registry)
    {
        AssetGraphSnapshot snapshot;
        if (const auto schema = wz::json::read_string(root, "schema")) {
            snapshot.schema = std::string(*schema);
        }
        snapshot.zoom = read_zoom(root);

        const auto layout = read_layout(root);
        std::unordered_map<
            wz::asset::AssetGraphDraftNodeId,
            wz::asset::AssetType>
            node_types;
        const auto changed_nodes = changed_nodes_for_status(draft);
        node_types.reserve(draft.nodes.size());
        for (const wz::asset::AssetGraphDraftNode& draft_node :
             draft.nodes)
        {
            if (draft_node.state
                == wz::asset::AssetGraphDraftNodeState::Deleted)
            {
                continue;
            }
            node_types.emplace(draft_node.id, draft_node.node.type);
        }

        snapshot.nodes.reserve(draft.nodes.size());
        for (std::size_t i = 0; i < draft.nodes.size(); ++i) {
            const wz::asset::AssetGraphDraftNode& draft_node =
                draft.nodes[i];
            if (draft_node.state
                == wz::asset::AssetGraphDraftNodeState::Deleted)
            {
                continue;
            }

            LayoutPosition position = fallback_position(i);
            if (const auto it = layout.find(draft_node.id);
                it != layout.end())
            {
                position = it->second;
            }

            const wz::asset::AssetNode& node = draft_node.node;
            std::vector<AssetGraphSnapshotPort> input_ports =
                registry
                    ? declared_input_ports_for_node(*registry, node)
                    : fallback_input_ports_for_node(
                        draft,
                        draft_node.id,
                        node_types);

            const std::string output_type = type_name(node.type);
            std::vector<AssetGraphSnapshotDiagnostic> diagnostics =
                diagnostics_for_node(draft, draft_node.id);
            snapshot.nodes.push_back(AssetGraphSnapshotNode{
                .id = draft_node.id,
                .type = node.type,
                .type_name = output_type,
                .schema = node.schema,
                .schema_label = schema_tail(node.schema),
                .display_name = display_name(node),
                .compile_status = compile_status_for_node(
                    diagnostics,
                    changed_nodes,
                    draft_node.id),
                .x = position.x,
                .y = position.y,
                .input_ports = std::move(input_ports),
                .output_ports = { AssetGraphSnapshotPort{
                    .index = 0u,
                    .type = node.type,
                    .name = "output",
                    .label = output_type,
                    .type_name = output_type,
                    .required = true,
                    .many = true,
                } },
                .diagnostics = std::move(diagnostics),
                .params = params_for_node(registry, node),
            });
        }

        snapshot.edges.reserve(draft.edges.size());
        for (const wz::asset::AssetGraphDraftEdge& edge : draft.edges) {
            snapshot.edges.push_back(AssetGraphSnapshotEdge{
                .id = edge.id,
                .from = edge.from,
                .to = edge.to,
                .to_input_port = edge.to_input_port,
            });
        }

        return snapshot;
    }

    AssetGraphSnapshotLoadResult load_project_asset_graph_snapshot(
        const wz::engine::project::ProjectManifestLoadDesc& desc)
    {
        AssetGraphSnapshotLoadResult result;

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

        const wz::json::JSONParseResult parsed =
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

        result.snapshot =
            build_asset_graph_snapshot(*parsed.document.root, draft);
        result.ok = true;
        return result;
    }

    AssetGraphSnapshotLoadResult load_project_asset_graph_snapshot(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        const wz::asset::CompilerRegistry& registry)
    {
        AssetGraphSnapshotLoadResult result;

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

        const wz::json::JSONParseResult parsed =
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

        result.snapshot =
            build_asset_graph_snapshot(*parsed.document.root, draft, &registry);
        result.ok = true;
        return result;
    }
}
