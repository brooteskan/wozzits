// src/engine/assets/scene/asset_graph_json.cpp
//
// Lifted from the scene editor (scene_editor_main_paths_and_project.inc) so the
// app and the editor share one v2 asset-graph-JSON parser.

#include <engine/assets/scene/asset_graph_json.h>

#include <engine/assets/engine_asset_library_internal.h>

#include <asset/types.h>

#include <external/json/json_read_helpers.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        bool parse_project_asset_key_text(
            const std::string& raw, wz::asset::AssetKey& out)
        {
            const auto first = raw.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                out = {};
                return true;
            }
            const auto last = raw.find_last_not_of(" \t\r\n");
            std::string text = raw.substr(first, last - first + 1);

            constexpr std::string_view prefix = "asset-key:";
            if (!text.starts_with(prefix)) {
                return false;
            }
            text.erase(0u, prefix.size());

            std::array<uint64_t, 8> parts{};
            for (std::size_t i = 0; i < parts.size(); ++i) {
                const std::size_t end = text.find(':');
                const std::string part =
                    end == std::string::npos ? text : text.substr(0u, end);
                if (part.empty()) {
                    return false;
                }

                char* parse_end = nullptr;
                errno = 0;
                const unsigned long long value =
                    std::strtoull(part.c_str(), &parse_end, 16);
                if (errno != 0 || !parse_end
                    || parse_end == part.c_str() || *parse_end != '\0')
                {
                    return false;
                }
                parts[i] = static_cast<uint64_t>(value);

                if (i + 1u == parts.size()) {
                    if (end != std::string::npos) {
                        return false;
                    }
                }
                else {
                    if (end == std::string::npos) {
                        return false;
                    }
                    text.erase(0u, end + 1u);
                }
            }

            out = wz::asset::AssetKey{
                .content_hash = { parts[0], parts[1] },
                .schema_hash = { parts[2], parts[3] },
                .compiler_hash = { parts[4], parts[5] },
                .deps_hash = { parts[6], parts[7] },
            };
            return true;
        }

        bool parse_schema_id_text(std::string_view text, wz::asset::SchemaID& out)
        {
            if (text.size() > 2u && text[0] == '0'
                && (text[1] == 'x' || text[1] == 'X'))
            {
                text.remove_prefix(2u);
            }
            std::string owned(text);
            char* end = nullptr;
            errno = 0;
            const unsigned long long value =
                std::strtoull(owned.c_str(), &end, 16);
            if (errno != 0 || !end || end == owned.c_str() || *end != '\0') {
                return false;
            }
            out.value = static_cast<uint64_t>(value);
            return true;
        }

        std::optional<uint64_t> read_uint64_json(
            const wz::json::JSONValue& value, std::string_view name)
        {
            const auto number = wz::json::read_number(value, name);
            if (!number || *number < 0.0) {
                return std::nullopt;
            }
            return static_cast<uint64_t>(*number);
        }

        bool read_param_value_json(
            const wz::json::JSONValue& value,
            std::string& name,
            wz::asset::ParamValue& out)
        {
            if (value.kind != wz::json::JSONValueKind::Object) {
                return false;
            }
            const auto name_value = wz::json::read_string(value, "name");
            const auto type_value = wz::json::read_string(value, "type");
            const wz::json::JSONValue* raw =
                wz::json::find_member(value, "value");
            if (!name_value || !type_value || !raw) {
                return false;
            }

            name = std::string(*name_value);
            const std::string_view type = *type_value;
            if (type == "bool") {
                if (raw->kind != wz::json::JSONValueKind::Bool) {
                    return false;
                }
                out = raw->bool_value;
                return true;
            }
            if (type == "int") {
                if (raw->kind != wz::json::JSONValueKind::Number) {
                    return false;
                }
                out = static_cast<int64_t>(raw->number_value);
                return true;
            }
            if (type == "float") {
                if (raw->kind != wz::json::JSONValueKind::Number) {
                    return false;
                }
                out = raw->number_value;
                return true;
            }
            if (type == "float3") {
                if (raw->kind != wz::json::JSONValueKind::Array
                    || raw->array_values.size() != 3u)
                {
                    return false;
                }
                std::array<float, 3> components{};
                for (size_t i = 0; i < components.size(); ++i) {
                    const auto& element = raw->array_values[i];
                    if (!element
                        || element->kind != wz::json::JSONValueKind::Number)
                    {
                        return false;
                    }
                    components[i] = static_cast<float>(element->number_value);
                }
                out = components;
                return true;
            }
            if (type == "string") {
                if (raw->kind != wz::json::JSONValueKind::String) {
                    return false;
                }
                out = raw->string_value;
                return true;
            }
            return false;
        }

        std::optional<wz::asset::ParamBlock> read_param_block_json(
            const wz::json::JSONValue& value)
        {
            if (value.kind != wz::json::JSONValueKind::Array) {
                return std::nullopt;
            }

            wz::asset::ParamBlock block{};
            for (const auto& item : value.array_values) {
                if (!item) {
                    return std::nullopt;
                }
                std::string name;
                wz::asset::ParamValue param{};
                if (!read_param_value_json(*item, name, param)) {
                    return std::nullopt;
                }
                block.values[name] = std::move(param);
            }
            return block;
        }

        const wz::json::JSONValue* asset_graph_node_meta_json(
            const wz::json::JSONValue& value, std::string_view name)
        {
            if (const auto* meta = wz::json::find_member(value, "meta")) {
                if (meta->kind == wz::json::JSONValueKind::Object) {
                    if (const auto* nested = wz::json::find_member(*meta, name)) {
                        return nested;
                    }
                }
            }
            return wz::json::find_member(value, name);
        }

        bool read_asset_graph_node_json(
            const wz::json::JSONValue& item,
            wz::asset::AssetNode& node,
            std::string& error)
        {
            if (const auto key_text = wz::json::read_string(item, "key")) {
                if (!parse_project_asset_key_text(std::string(*key_text), node.key)) {
                    error = "invalid node key";
                    return false;
                }
            }

            node.type = static_cast<wz::asset::AssetType>(
                static_cast<uint16_t>(
                    wz::json::read_number(item, "type").value_or(0.0)));
            if (const auto schema_text = wz::json::read_string(item, "schema")) {
                if (!parse_schema_id_text(*schema_text, node.schema)) {
                    error = "invalid schema";
                    return false;
                }
            }
            node.stage = static_cast<wz::asset::AssetStage>(
                static_cast<uint8_t>(
                    wz::json::read_number(item, "stage").value_or(0.0)));
            node.residency = static_cast<wz::asset::ResidencyIntent>(
                static_cast<uint8_t>(
                    wz::json::read_number(item, "residency").value_or(0.0)));
            node.kind = static_cast<wz::asset::AssetNodeKind>(
                static_cast<uint8_t>(
                    wz::json::read_number(item, "kind").value_or(0.0)));
            node.demand_root = static_cast<wz::asset::DemandRoot>(
                static_cast<uint8_t>(
                    wz::json::read_number(item, "demand_root").value_or(0.0)));
            node.payload = std::vector<uint8_t>{};

            if (const auto* params = asset_graph_node_meta_json(item, "params")) {
                auto block = read_param_block_json(*params);
                if (!block) {
                    error = "invalid params";
                    return false;
                }
                node.meta = std::move(*block);
            }
            else if (const auto* file_source =
                         asset_graph_node_meta_json(item, "file_source"))
            {
                if (file_source->kind != wz::json::JSONValueKind::Object) {
                    error = "invalid file source";
                    return false;
                }
                std::string full_path;
                std::string canonical_path;
                if (const auto full =
                        wz::json::read_string(*file_source, "full_path"))
                {
                    full_path = std::string(*full);
                }
                if (const auto canonical =
                        wz::json::read_string(*file_source, "canonical_path"))
                {
                    canonical_path = std::string(*canonical);
                }
                node.meta = wz::engine::assets::internal::FileSourceDesc{
                    .full_path = full_path,
                    .canonical_path = canonical_path,
                };
            }

            return true;
        }
    }

    bool load_asset_graph_draft_from_v2_json(
        const wz::json::JSONValue& root,
        wz::asset::AssetGraphDraft& draft,
        std::string& error)
    {
        const auto* nodes = wz::json::find_member(root, "nodes");
        if (!nodes || nodes->kind != wz::json::JSONValueKind::Array) {
            error = "missing nodes";
            return false;
        }

        std::vector<wz::asset::AuthoredGraphNode> authored_nodes;
        std::vector<wz::asset::AuthoredGraphEdge> authored_edges;
        authored_nodes.reserve(nodes->array_values.size());

        for (const auto& item : nodes->array_values) {
            if (!item || item->kind != wz::json::JSONValueKind::Object) {
                error = "malformed node";
                return false;
            }

            const auto node_id = read_uint64_json(*item, "node_id");
            if (!node_id
                || *node_id == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE)
            {
                error = "invalid node id";
                return false;
            }

            wz::asset::AssetNode node{};
            std::string node_error;
            if (!read_asset_graph_node_json(*item, node, node_error)) {
                error = node_error;
                return false;
            }

            authored_nodes.push_back(wz::asset::AuthoredGraphNode{
                .node_id = *node_id,
                .node = std::move(node),
            });

            if (const auto* deps = wz::json::find_member(*item, "deps")) {
                if (deps->kind != wz::json::JSONValueKind::Array) {
                    error = "invalid deps";
                    return false;
                }
                for (const auto& dep : deps->array_values) {
                    if (!dep || dep->kind != wz::json::JSONValueKind::Object) {
                        error = "invalid dep";
                        return false;
                    }
                    const auto from_node_id =
                        read_uint64_json(*dep, "from_node_id");
                    if (!from_node_id
                        || *from_node_id
                            == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE)
                    {
                        error = "invalid dep node";
                        return false;
                    }
                    authored_edges.push_back(wz::asset::AuthoredGraphEdge{
                        .from = *from_node_id,
                        .to = *node_id,
                        .to_input_port = static_cast<uint32_t>(
                            wz::json::read_number(*dep, "to_input_port")
                                .value_or(0.0)),
                    });
                }
            }
        }

        wz::asset::load_asset_graph_draft_from_authored(
            draft,
            std::span<const wz::asset::AuthoredGraphNode>(
                authored_nodes.data(), authored_nodes.size()),
            std::span<const wz::asset::AuthoredGraphEdge>(
                authored_edges.data(), authored_edges.size()));

        if (draft.nodes.size() != authored_nodes.size()
            || draft.edges.size() != authored_edges.size())
        {
            error = "invalid authored graph ids";
            return false;
        }

        return true;
    }
}
