// src/engine/assets/scene/asset_graph_json.cpp
//
// Lifted from the scene editor (scene_editor_main_paths_and_project.inc) so the
// app and the editor share one v2 asset-graph-JSON parser.

#include <engine/assets/scene/asset_graph_json.h>

#include <engine/assets/engine_asset_library_internal.h>

#include <asset/types.h>

#include <external/json/json_read_helpers.h>
#include <external/json/json_writer.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        // One constant for the reader and the writer, so the gate below and the
        // string emitted by save_asset_graph_draft_to_v2_json cannot drift.
        constexpr std::string_view kAssetGraphV2Schema =
            "wozzits.scene_editor.assets.graph.v2";

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

        // Exact non-negative integers only. The old body guarded `< 0.0` and
        // then cast, so `1e300` was UB that landed on 9223372036854775808 and
        // `1.5` silently became node 1 -- a graph edge pointing somewhere the
        // author never named. Both now read as absent, which every caller
        // already treats as a hard error ("invalid node id").
        std::optional<uint64_t> read_uint64_json(
            const wz::json::JSONValue& value, std::string_view name)
        {
            const auto number = wz::json::read_number(value, name);
            if (!number || *number != std::trunc(*number)) {
                return std::nullopt;
            }
            return wz::json::narrow_number<uint64_t>(*number);
        }

        // Same treatment for the small enum-ish fields, which were read as
        // `read_number(...).value_or(0.0)` and cast straight to their storage
        // width: an out-of-range value became a well-defined but non-existent
        // enumerator. Out of range now reads as the 0 default, matching the
        // absent case.
        template <typename T>
        T read_enum_scalar_json(
            const wz::json::JSONValue& value, std::string_view name)
        {
            const auto raw = wz::json::read_number(value, name);
            if (!raw || *raw != std::trunc(*raw)) {
                return T{};
            }
            const auto narrowed = wz::json::narrow_number<T>(*raw);
            return narrowed ? *narrowed : T{};
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
                // Reject a double that cannot become an int64 -- a raw
                // static_cast of an out-of-range double is UB. (C1-C22, #314)
                // The hand-rolled bound below was off by one: the literal
                // 9.2233720368547758e18 rounds to EXACTLY 2^63, and int64 max is
                // 2^63-1, so `> 2^63` let v == 2^63 through to a UB cast that lands
                // on INT64_MIN. narrow_number<int64_t> uses `truncated >= 2^63`
                // and is what the sibling uint64/enum branches already use.
                // (A3-C4b, #77 visit 2)
                if (auto n = wz::json::narrow_number<int64_t>(raw->number_value)) {
                    out = *n;
                    return true;
                }
                return false;
            }
            if (type == "float") {
                if (raw->kind != wz::json::JSONValueKind::Number) {
                    return false;
                }
                // narrow_float rejects a value that overflows or NaNs a float --
                // 1e308 is legal JSON but becomes +inf at every consumer, and the
                // guard already sits at the top of this include. (C1-C21, #314)
                if (!wz::json::narrow_float(raw->number_value)) {
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
                    const auto narrowed =
                        wz::json::narrow_float(element->number_value);
                    if (!narrowed) {  // reject 1e308/NaN per element (C1-C22)
                        return false;
                    }
                    components[i] = *narrowed;
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
                read_enum_scalar_json<uint16_t>(item, "type"));
            if (const auto schema_text = wz::json::read_string(item, "schema")) {
                if (!parse_schema_id_text(*schema_text, node.schema)) {
                    error = "invalid schema";
                    return false;
                }
            }
            node.stage = static_cast<wz::asset::AssetStage>(
                read_enum_scalar_json<uint8_t>(item, "stage"));
            node.residency = static_cast<wz::asset::ResidencyIntent>(
                read_enum_scalar_json<uint8_t>(item, "residency"));
            node.kind = static_cast<wz::asset::AssetNodeKind>(
                read_enum_scalar_json<uint8_t>(item, "kind"));
            node.demand_root = static_cast<wz::asset::DemandRoot>(
                read_enum_scalar_json<uint8_t>(item, "demand_root"));
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
        // Read back the schema this loader's own writer emits. Without this, any
        // object with a "nodes" array loaded as a v2 graph -- a v1 document (they
        // exist: resources/projects/dag_trials) failed with the unrelated
        // "invalid node id" because v1 nodes carry no node_id, and a future v3
        // would be read under v2 key meanings with no diagnostic at all.
        //
        // Deliberately asymmetric, matching the statechart gate: a WRONG schema
        // is refused, a MISSING one is accepted. Rejecting the missing case buys
        // nothing (a document that omits it is hand-written, not a different
        // dialect) and would break every caller that builds a graph document
        // inline.
        if (const auto schema = wz::json::read_string(root, "schema")) {
            if (*schema != kAssetGraphV2Schema) {
                error = "asset graph schema is '" + std::string(*schema)
                    + "', expected '" + std::string(kAssetGraphV2Schema) + "'";
                return false;
            }
        }

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

            // 0 is refused alongside the INVALID sentinel: the draft allocator
            // starts at 1 (draft.h next_node_id) and BOTH scene-side readers
            // treat 0 as "no id", so a node numbered 0 is one nothing in the
            // project can reference. This reader was the last place that let one
            // in.
            const auto node_id = read_uint64_json(*item, "node_id");
            if (!node_id || *node_id == 0u
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
                        .to_input_port =
                            read_enum_scalar_json<uint32_t>(
                                *dep, "to_input_port"),
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

    namespace
    {
        // ---- v2 asset-graph JSON writers ------------------------------------
        // Lifted from the wozzits-imgui scene editor
        // (scene_editor_main_paths_and_project.inc) — the inverse of the readers
        // above, so the two stay one format.

        wz::json::JSONValuePtr json_number(double value)
        {
            auto out = std::make_unique<wz::json::JSONValue>();
            out->kind = wz::json::JSONValueKind::Number;
            out->number_value = value;
            return out;
        }

        wz::json::JSONValuePtr json_string(std::string value)
        {
            auto out = std::make_unique<wz::json::JSONValue>();
            out->kind = wz::json::JSONValueKind::String;
            out->string_value = std::move(value);
            return out;
        }

        wz::json::JSONValuePtr json_bool(bool value)
        {
            auto out = std::make_unique<wz::json::JSONValue>();
            out->kind = wz::json::JSONValueKind::Bool;
            out->bool_value = value;
            return out;
        }

        wz::json::JSONValuePtr json_object()
        {
            auto out = std::make_unique<wz::json::JSONValue>();
            out->kind = wz::json::JSONValueKind::Object;
            return out;
        }

        wz::json::JSONValuePtr json_array()
        {
            auto out = std::make_unique<wz::json::JSONValue>();
            out->kind = wz::json::JSONValueKind::Array;
            return out;
        }

        void json_add_member(
            wz::json::JSONValue& object,
            std::string key,
            wz::json::JSONValuePtr value)
        {
            if (object.kind != wz::json::JSONValueKind::Object || !value) {
                return;
            }
            object.object_members.push_back(wz::json::JSONMember{
                .key = std::move(key),
                .value = std::move(value),
            });
        }

        std::string project_asset_key_text(const wz::asset::AssetKey& key)
        {
            if (key == wz::asset::AssetKey{}) {
                return {};
            }

            char buffer[192]{};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "asset-key:%016llx:%016llx:%016llx:%016llx:%016llx:%016llx:"
                "%016llx:%016llx",
                static_cast<unsigned long long>(key.content_hash.lo),
                static_cast<unsigned long long>(key.content_hash.hi),
                static_cast<unsigned long long>(key.schema_hash.lo),
                static_cast<unsigned long long>(key.schema_hash.hi),
                static_cast<unsigned long long>(key.compiler_hash.lo),
                static_cast<unsigned long long>(key.compiler_hash.hi),
                static_cast<unsigned long long>(key.deps_hash.lo),
                static_cast<unsigned long long>(key.deps_hash.hi));
            return buffer;
        }

        std::string schema_id_text(wz::asset::SchemaID schema)
        {
            char buffer[32]{};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "0x%016llx",
                static_cast<unsigned long long>(schema.value));
            return buffer;
        }

        wz::json::JSONValuePtr param_value_json(
            const std::string& name,
            const wz::asset::ParamValue& value)
        {
            auto out = json_object();
            json_add_member(*out, "name", json_string(name));
            std::visit(
                [&](const auto& typed)
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::is_same_v<T, bool>) {
                        json_add_member(*out, "type", json_string("bool"));
                        json_add_member(*out, "value", json_bool(typed));
                    }
                    else if constexpr (std::is_same_v<T, int64_t>) {
                        json_add_member(*out, "type", json_string("int"));
                        json_add_member(
                            *out,
                            "value",
                            json_number(static_cast<double>(typed)));
                    }
                    else if constexpr (std::is_same_v<T, double>) {
                        json_add_member(*out, "type", json_string("float"));
                        json_add_member(*out, "value", json_number(typed));
                    }
                    else if constexpr (
                        std::is_same_v<T, std::array<float, 3>>)
                    {
                        auto array = json_array();
                        array->array_values.push_back(json_number(typed[0]));
                        array->array_values.push_back(json_number(typed[1]));
                        array->array_values.push_back(json_number(typed[2]));
                        json_add_member(*out, "type", json_string("float3"));
                        json_add_member(*out, "value", std::move(array));
                    }
                    else if constexpr (std::is_same_v<T, std::string>) {
                        json_add_member(*out, "type", json_string("string"));
                        json_add_member(*out, "value", json_string(typed));
                    }
                },
                value);
            return out;
        }

        wz::json::JSONValuePtr param_block_json(
            const wz::asset::ParamBlock& block)
        {
            auto params = json_array();
            std::vector<std::string> names;
            names.reserve(block.values.size());
            for (const auto& entry : block.values) {
                names.push_back(entry.first);
            }
            std::sort(names.begin(), names.end());

            for (const auto& name : names) {
                params->array_values.push_back(
                    param_value_json(name, block.values.at(name)));
            }
            return params;
        }

        wz::json::JSONValuePtr asset_node_meta_json(
            const wz::asset::AssetNode& node)
        {
            auto meta = json_object();
            bool has_meta = false;
            if (const auto* file_source = std::any_cast<
                    wz::engine::assets::internal::FileSourceDesc>(&node.meta))
            {
                auto file_json = json_object();
                json_add_member(
                    *file_json,
                    "full_path",
                    json_string(file_source->full_path));
                json_add_member(
                    *file_json,
                    "canonical_path",
                    json_string(file_source->canonical_path));
                json_add_member(*meta, "file_source", std::move(file_json));
                has_meta = true;
            }
            else if (const auto* params =
                         std::any_cast<wz::asset::ParamBlock>(&node.meta))
            {
                json_add_member(*meta, "params", param_block_json(*params));
                has_meta = true;
            }
            return has_meta ? std::move(meta) : nullptr;
        }

        wz::json::JSONValuePtr asset_graph_node_json(
            const wz::asset::AssetGraphDraftNode& node,
            const wz::asset::AssetGraphDraft& draft)
        {
            auto node_json = json_object();
            json_add_member(
                *node_json,
                "node_id",
                json_number(static_cast<double>(node.id)));
            if (!(node.node.key == wz::asset::AssetKey{})) {
                json_add_member(
                    *node_json,
                    "key",
                    json_string(project_asset_key_text(node.node.key)));
            }
            json_add_member(
                *node_json,
                "type",
                json_number(static_cast<uint16_t>(node.node.type)));
            json_add_member(
                *node_json,
                "schema",
                json_string(schema_id_text(node.node.schema)));
            json_add_member(
                *node_json,
                "stage",
                json_number(static_cast<uint8_t>(node.node.stage)));
            json_add_member(
                *node_json,
                "residency",
                json_number(static_cast<uint8_t>(node.node.residency)));
            json_add_member(
                *node_json,
                "kind",
                json_number(static_cast<uint8_t>(node.node.kind)));
            json_add_member(
                *node_json,
                "demand_root",
                json_number(static_cast<uint8_t>(node.node.demand_root)));

            auto deps = json_array();
            for (const auto& edge : draft.edges) {
                if (edge.to != node.id) {
                    continue;
                }
                auto dep_json = json_object();
                json_add_member(
                    *dep_json,
                    "from_node_id",
                    json_number(static_cast<double>(edge.from)));
                json_add_member(
                    *dep_json,
                    "to_input_port",
                    json_number(static_cast<double>(edge.to_input_port)));
                deps->array_values.push_back(std::move(dep_json));
            }
            json_add_member(*node_json, "deps", std::move(deps));

            if (auto meta = asset_node_meta_json(node.node)) {
                json_add_member(*node_json, "meta", std::move(meta));
            }

            return node_json;
        }
    }

    std::string save_asset_graph_draft_to_v2_json(
        const wz::asset::AssetGraphDraft& draft)
    {
        wz::json::JSONValue root;
        root.kind = wz::json::JSONValueKind::Object;
        json_add_member(
            root,
            "schema",
            json_string(std::string(kAssetGraphV2Schema)));
        json_add_member(root, "version", json_number(2));

        auto nodes = json_array();
        for (const auto& node : draft.nodes) {
            if (node.state == wz::asset::AssetGraphDraftNodeState::Deleted) {
                continue;
            }
            nodes->array_values.push_back(asset_graph_node_json(node, draft));
        }
        json_add_member(root, "nodes", std::move(nodes));

        return wz::json::serialize_json(root);
    }
}
