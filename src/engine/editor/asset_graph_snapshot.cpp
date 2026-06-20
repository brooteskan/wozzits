#include <engine/editor/asset_graph_snapshot.h>

#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/type_extensions.h>

#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <any>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
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
            snapshot.nodes.push_back(AssetGraphSnapshotNode{
                .id = draft_node.id,
                .type = node.type,
                .type_name = output_type,
                .schema = node.schema,
                .schema_label = schema_tail(node.schema),
                .display_name = display_name(node),
                .compile_status = "not resolved",
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
