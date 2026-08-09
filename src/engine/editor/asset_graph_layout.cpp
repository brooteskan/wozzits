#include <engine/editor/asset_graph_layout.h>

#include <engine/assets/scene/asset_graph_json.h>

#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>
#include <external/json/json_writer.h>

#include <cmath>
#include <iterator>
#include <memory>
#include <string_view>
#include <utility>

namespace wz::engine::editor
{
    namespace
    {
        constexpr double kMaxAbsGraphCoordinate = 100000.0;
        constexpr double kMinGraphZoom = 0.25;
        constexpr double kMaxGraphZoom = 4.0;

        std::string read_text_file(const wz::fs::Path& path)
        {
            // Through wz::fs so a non-ASCII path reads correctly (a narrow
            // std::ifstream(path) would misread it on Windows). {} on failure.
            const wz::fs::FileResult<std::string> result =
                wz::fs::read_file_text(path);
            return result ? result.value : std::string{};
        }

        bool write_text_file(const wz::fs::Path& path, const std::string& text)
        {
            // Through wz::fs: UTF-8-correct path + a checked, chunked write (the old
            // stream reported success before the filebuf flushed on destruction).
            return wz::fs::write_file_text(path, text) == wz::fs::FileError::None;
        }

        wz::fs::Path resolve_resource_path(
            const wz::fs::Path& resource_root,
            const wz::fs::Path& path)
        {
            return wz::fs::is_absolute(path) || resource_root.empty()
                ? path
                : wz::fs::join(resource_root, path);
        }

        AssetGraphLayoutUpdateResult failure(std::string error)
        {
            return AssetGraphLayoutUpdateResult{
                .ok = false,
                .error = std::move(error),
            };
        }

        wz::json::JSONValue* find_member_mut(
            wz::json::JSONValue& obj,
            std::string_view key)
        {
            if (obj.kind != wz::json::JSONValueKind::Object) {
                return nullptr;
            }
            for (auto& member : obj.object_members) {
                if (member.key == key && member.value) {
                    return member.value.get();
                }
            }
            return nullptr;
        }

        void reset_value(
            wz::json::JSONValue& value,
            wz::json::JSONValueKind kind)
        {
            value.kind = kind;
            value.bool_value = false;
            value.number_value = 0.0;
            value.string_value.clear();
            value.array_values.clear();
            value.object_members.clear();
        }

        wz::json::JSONValue& ensure_object_member(
            wz::json::JSONValue& obj,
            std::string key)
        {
            if (wz::json::JSONValue* existing = find_member_mut(obj, key)) {
                if (existing->kind != wz::json::JSONValueKind::Object) {
                    reset_value(*existing, wz::json::JSONValueKind::Object);
                }
                return *existing;
            }

            auto value = std::make_unique<wz::json::JSONValue>();
            value->kind = wz::json::JSONValueKind::Object;
            wz::json::JSONValue& ref = *value;
            obj.object_members.push_back(wz::json::JSONMember{
                .key = std::move(key),
                .value = std::move(value),
            });
            return ref;
        }

        wz::json::JSONValue& ensure_array_member(
            wz::json::JSONValue& obj,
            std::string key)
        {
            if (wz::json::JSONValue* existing = find_member_mut(obj, key)) {
                if (existing->kind != wz::json::JSONValueKind::Array) {
                    reset_value(*existing, wz::json::JSONValueKind::Array);
                }
                return *existing;
            }

            auto value = std::make_unique<wz::json::JSONValue>();
            value->kind = wz::json::JSONValueKind::Array;
            wz::json::JSONValue& ref = *value;
            obj.object_members.push_back(wz::json::JSONMember{
                .key = std::move(key),
                .value = std::move(value),
            });
            return ref;
        }

        void set_number_member(
            wz::json::JSONValue& obj,
            std::string key,
            double value)
        {
            wz::json::JSONValue* existing = find_member_mut(obj, key);
            if (!existing) {
                auto member_value = std::make_unique<wz::json::JSONValue>();
                member_value->kind = wz::json::JSONValueKind::Number;
                member_value->number_value = value;
                obj.object_members.push_back(wz::json::JSONMember{
                    .key = std::move(key),
                    .value = std::move(member_value),
                });
                return;
            }

            reset_value(*existing, wz::json::JSONValueKind::Number);
            existing->number_value = value;
        }

        wz::json::JSONValue* find_layout_node(
            wz::json::JSONValue& nodes,
            wz::asset::AssetGraphDraftNodeId node_id)
        {
            if (nodes.kind != wz::json::JSONValueKind::Array) {
                return nullptr;
            }

            for (auto& item : nodes.array_values) {
                if (!item || item->kind != wz::json::JSONValueKind::Object) {
                    continue;
                }
                const auto existing_id =
                    wz::json::read_number(*item, "node_id");
                if (existing_id
                    && static_cast<wz::asset::AssetGraphDraftNodeId>(
                           *existing_id)
                        == node_id)
                {
                    return item.get();
                }
            }
            return nullptr;
        }

        wz::json::JSONValue& append_layout_node(
            wz::json::JSONValue& nodes,
            wz::asset::AssetGraphDraftNodeId node_id)
        {
            auto value = std::make_unique<wz::json::JSONValue>();
            value->kind = wz::json::JSONValueKind::Object;
            wz::json::JSONValue& ref = *value;
            set_number_member(ref, "node_id", static_cast<double>(node_id));
            nodes.array_values.push_back(std::move(value));
            return ref;
        }
    }

    void apply_asset_graph_layout_node(
        wz::json::JSONValue& graph_root,
        wz::asset::AssetGraphDraftNodeId node_id,
        double x,
        double y)
    {
        wz::json::JSONValue& layout =
            ensure_object_member(graph_root, "layout");
        set_number_member(layout, "version", 2.0);
        wz::json::JSONValue& nodes =
            ensure_array_member(layout, "nodes");
        wz::json::JSONValue* existing_layout_node =
            find_layout_node(nodes, node_id);
        wz::json::JSONValue& layout_node = existing_layout_node
            ? *existing_layout_node
            : append_layout_node(nodes, node_id);

        set_number_member(layout_node, "x", x);
        set_number_member(layout_node, "y", y);
    }

    void apply_asset_graph_layout_zoom(
        wz::json::JSONValue& graph_root,
        double zoom)
    {
        wz::json::JSONValue& layout =
            ensure_object_member(graph_root, "layout");
        set_number_member(layout, "version", 2.0);
        set_number_member(layout, "zoom", zoom);
    }

    AssetGraphLayoutUpdateResult update_project_asset_graph_node_layout(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        wz::asset::AssetGraphDraftNodeId node_id,
        double x,
        double y)
    {
        if (node_id == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE) {
            return failure("asset graph node id is invalid");
        }
        if (!std::isfinite(x) || !std::isfinite(y)
            || std::fabs(x) > kMaxAbsGraphCoordinate
            || std::fabs(y) > kMaxAbsGraphCoordinate)
        {
            return failure("asset graph node position is invalid");
        }

        const auto loaded = wz::engine::project::load_project_manifest(desc);
        if (!loaded.ok) {
            return failure(loaded.error);
        }
        if (loaded.manifest.asset_graph_path.empty()) {
            return failure("project manifest does not define an asset graph");
        }

        const wz::fs::Path graph_path = resolve_resource_path(
            desc.resource_root,
            loaded.manifest.asset_graph_path);
        const std::string graph_text = read_text_file(graph_path);
        if (graph_text.empty()) {
            return failure(
                "cannot read asset graph: " + loaded.manifest.asset_graph_path);
        }

        wz::json::JSONParseResult parsed =
            wz::json::parse_json_string(graph_text);
        if (!parsed.ok || !parsed.document.root) {
            return failure("invalid asset graph json");
        }
        if (parsed.document.root->kind != wz::json::JSONValueKind::Object) {
            return failure("asset graph root must be an object");
        }

        wz::asset::AssetGraphDraft draft;
        std::string parse_error;
        if (!wz::engine::assets::load_asset_graph_draft_from_v2_json(
                *parsed.document.root,
                draft,
                parse_error))
        {
            return failure("asset graph parse failed: " + parse_error);
        }
        if (!wz::asset::find_asset_graph_draft_node(draft, node_id)) {
            return failure("asset graph node does not exist");
        }

        apply_asset_graph_layout_node(*parsed.document.root, node_id, x, y);

        if (!write_text_file(
                graph_path,
                wz::json::serialize_json(parsed.document)))
        {
            return failure(
                "cannot write asset graph: " + loaded.manifest.asset_graph_path);
        }

        return AssetGraphLayoutUpdateResult{ .ok = true };
    }

    AssetGraphLayoutUpdateResult save_project_asset_graph(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        const wz::asset::AssetGraphDraft& draft,
        const wz::json::JSONValue* layout_source)
    {
        const auto loaded = wz::engine::project::load_project_manifest(desc);
        if (!loaded.ok) {
            return failure(loaded.error);
        }
        if (loaded.manifest.asset_graph_path.empty()) {
            return failure("project manifest does not define an asset graph");
        }

        const wz::fs::Path graph_path = resolve_resource_path(
            desc.resource_root,
            loaded.manifest.asset_graph_path);

        // Serialize the draft to a fresh v2 document (graph topology + node data).
        wz::json::JSONParseResult written = wz::json::parse_json_string(
            wz::engine::assets::save_asset_graph_draft_to_v2_json(draft));
        if (!written.ok || !written.document.root
            || written.document.root->kind != wz::json::JSONValueKind::Object)
        {
            return failure("failed to serialize asset graph draft");
        }

        // Carry the "layout" object (node positions + zoom), which the draft does
        // not hold. Prefer the in-memory session layout (layout_source) so unsaved
        // node positions survive; otherwise preserve the existing on-disk layout.
        // The source is deep-copied via a serialize/parse round-trip.
        std::string layout_text;
        if (layout_source != nullptr
            && layout_source->kind == wz::json::JSONValueKind::Object)
        {
            for (const auto& member : layout_source->object_members) {
                if (member.key == "layout" && member.value) {
                    layout_text = wz::json::serialize_json(*member.value);
                    break;
                }
            }
        }
        else {
            const std::string existing_text = read_text_file(graph_path);
            if (!existing_text.empty()) {
                wz::json::JSONParseResult existing =
                    wz::json::parse_json_string(existing_text);
                if (existing.ok && existing.document.root
                    && existing.document.root->kind
                        == wz::json::JSONValueKind::Object)
                {
                    for (const auto& member :
                         existing.document.root->object_members)
                    {
                        if (member.key == "layout" && member.value) {
                            layout_text =
                                wz::json::serialize_json(*member.value);
                            break;
                        }
                    }
                }
            }
        }

        if (!layout_text.empty()) {
            wz::json::JSONParseResult layout_doc =
                wz::json::parse_json_string(layout_text);
            if (layout_doc.ok && layout_doc.document.root) {
                written.document.root->object_members.push_back(
                    wz::json::JSONMember{
                        .key = "layout",
                        .value = std::move(layout_doc.document.root),
                    });
            }
        }

        if (!write_text_file(
                graph_path,
                wz::json::serialize_json(written.document)))
        {
            return failure(
                "cannot write asset graph: " + loaded.manifest.asset_graph_path);
        }

        return AssetGraphLayoutUpdateResult{ .ok = true };
    }

    AssetGraphLayoutUpdateResult update_project_asset_graph_zoom(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        double zoom)
    {
        if (!std::isfinite(zoom)
            || zoom < kMinGraphZoom || zoom > kMaxGraphZoom)
        {
            return failure("asset graph zoom is invalid");
        }

        const auto loaded = wz::engine::project::load_project_manifest(desc);
        if (!loaded.ok) {
            return failure(loaded.error);
        }
        if (loaded.manifest.asset_graph_path.empty()) {
            return failure("project manifest does not define an asset graph");
        }

        const wz::fs::Path graph_path = resolve_resource_path(
            desc.resource_root,
            loaded.manifest.asset_graph_path);
        const std::string graph_text = read_text_file(graph_path);
        if (graph_text.empty()) {
            return failure(
                "cannot read asset graph: " + loaded.manifest.asset_graph_path);
        }

        wz::json::JSONParseResult parsed =
            wz::json::parse_json_string(graph_text);
        if (!parsed.ok || !parsed.document.root) {
            return failure("invalid asset graph json");
        }
        if (parsed.document.root->kind != wz::json::JSONValueKind::Object) {
            return failure("asset graph root must be an object");
        }

        wz::asset::AssetGraphDraft draft;
        std::string parse_error;
        if (!wz::engine::assets::load_asset_graph_draft_from_v2_json(
                *parsed.document.root,
                draft,
                parse_error))
        {
            return failure("asset graph parse failed: " + parse_error);
        }

        wz::json::JSONValue& layout =
            ensure_object_member(*parsed.document.root, "layout");
        set_number_member(layout, "version", 2.0);
        set_number_member(layout, "zoom", zoom);

        if (!write_text_file(
                graph_path,
                wz::json::serialize_json(parsed.document)))
        {
            return failure(
                "cannot write asset graph: " + loaded.manifest.asset_graph_path);
        }

        return AssetGraphLayoutUpdateResult{ .ok = true };
    }
}
