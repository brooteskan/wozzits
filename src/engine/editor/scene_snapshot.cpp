#include <engine/editor/scene_snapshot.h>

#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <cmath>
#include <iomanip>
#include <fstream>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wz::engine::editor
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846264338327950288;

        struct FlatSceneSnapshotNode
        {
            SceneSnapshotNode node;
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

        std::vector<double> read_number_array(
            const wz::json::JSONValue& obj,
            std::string_view key)
        {
            std::vector<double> out;
            const auto* value = wz::json::find_member(obj, key);
            if (!value || value->kind != wz::json::JSONValueKind::Array) {
                return out;
            }

            out.reserve(value->array_values.size());
            for (const auto& item : value->array_values) {
                if (item && item->kind == wz::json::JSONValueKind::Number) {
                    out.push_back(item->number_value);
                }
            }
            return out;
        }

        std::string format_scene_number(double value)
        {
            if (std::abs(value) < 0.0005) {
                value = 0.0;
            }

            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << std::fixed << std::setprecision(3) << value;

            std::string text = out.str();
            while (text.size() > 1u && text.back() == '0') {
                text.pop_back();
            }
            if (!text.empty() && text.back() == '.') {
                text.pop_back();
            }
            return text == "-0" ? "0" : text;
        }

        std::string format_component(
            const std::vector<double>& values,
            size_t index)
        {
            return index < values.size()
                ? format_scene_number(values[index])
                : std::string{};
        }

        SceneSnapshotTransformDisplay transform_display(
            const std::vector<double>& translation,
            const std::vector<double>& rotation_euler_degrees,
            const std::vector<double>& scale)
        {
            return SceneSnapshotTransformDisplay{
                .translation_x = format_component(translation, 0u),
                .translation_y = format_component(translation, 1u),
                .translation_z = format_component(translation, 2u),
                .rotation_x = format_component(rotation_euler_degrees, 0u),
                .rotation_y = format_component(rotation_euler_degrees, 1u),
                .rotation_z = format_component(rotation_euler_degrees, 2u),
                .scale_x = format_component(scale, 0u),
                .scale_y = format_component(scale, 1u),
                .scale_z = format_component(scale, 2u),
            };
        }

        std::vector<double> rotation_euler_degrees_from_quat(
            const std::vector<double>& rotation_quat)
        {
            if (rotation_quat.size() < 4u) {
                return {};
            }

            double x = rotation_quat[0];
            double y = rotation_quat[1];
            double z = rotation_quat[2];
            double w = rotation_quat[3];
            const double length =
                std::sqrt((x * x) + (y * y) + (z * z) + (w * w));
            if (length <= std::numeric_limits<double>::epsilon()) {
                return {};
            }

            x /= length;
            y /= length;
            z /= length;
            w /= length;

            const double sinr_cosp = 2.0 * ((w * x) + (y * z));
            const double cosr_cosp = 1.0 - (2.0 * ((x * x) + (y * y)));
            const double roll = std::atan2(sinr_cosp, cosr_cosp);

            const double sinp = 2.0 * ((w * y) - (z * x));
            const double pitch = std::abs(sinp) >= 1.0
                ? std::copysign(kPi * 0.5, sinp)
                : std::asin(sinp);

            const double siny_cosp = 2.0 * ((w * z) + (x * y));
            const double cosy_cosp = 1.0 - (2.0 * ((y * y) + (z * z)));
            const double yaw = std::atan2(siny_cosp, cosy_cosp);

            constexpr double kRadiansToDegrees = 180.0 / kPi;
            return {
                roll * kRadiansToDegrees,
                pitch * kRadiansToDegrees,
                yaw * kRadiansToDegrees,
            };
        }

        std::optional<SceneSnapshotTransform> read_transform(
            const wz::json::JSONValue& obj)
        {
            const auto* transform =
                wz::json::find_member(obj, "transform");
            if (!transform
                || transform->kind != wz::json::JSONValueKind::Object)
            {
                return std::nullopt;
            }

            std::vector<double> rotation_quat =
                read_number_array(*transform, "rotation_quat");
            std::vector<double> translation =
                read_number_array(*transform, "translation");
            std::vector<double> rotation_euler_degrees =
                rotation_euler_degrees_from_quat(rotation_quat);
            std::vector<double> scale =
                read_number_array(*transform, "scale");
            return SceneSnapshotTransform{
                .translation = translation,
                .rotation_quat = rotation_quat,
                .rotation_euler_degrees = rotation_euler_degrees,
                .scale = scale,
                .display = transform_display(
                    translation,
                    rotation_euler_degrees,
                    scale),
            };
        }

        std::optional<SceneSnapshotCamera> read_camera(
            const wz::json::JSONValue& obj)
        {
            const auto* camera = wz::json::find_member(obj, "camera");
            if (!camera || camera->kind != wz::json::JSONValueKind::Object) {
                return std::nullopt;
            }

            return SceneSnapshotCamera{
                .fov_y = wz::json::read_number(*camera, "fov_y"),
                .near_plane = wz::json::read_number(*camera, "near"),
                .far_plane = wz::json::read_number(*camera, "far"),
                .aspect = wz::json::read_number(*camera, "aspect"),
            };
        }

        std::optional<SceneSnapshotRenderable> read_renderable(
            const wz::json::JSONValue& obj)
        {
            const auto* renderable = wz::json::find_member(obj, "renderable");
            if (!renderable
                || renderable->kind != wz::json::JSONValueKind::Object)
            {
                return std::nullopt;
            }

            SceneSnapshotRenderable out;
            if (const auto node_id =
                    wz::json::read_number(*renderable, "asset_graph_node_id");
                node_id && *node_id >= 0.0)
            {
                out.asset_graph_node_id =
                    static_cast<wz::asset::AssetGraphDraftNodeId>(*node_id);
            }
            out.source = out.asset_graph_node_id
                ? SceneSnapshotRenderableSource{
                    .kind = "asset-graph-node",
                    .display_name = "asset-node",
                }
                : SceneSnapshotRenderableSource{
                    .kind = "none",
                    .display_name = "none",
                };
            return out;
        }

        std::string node_kind(const SceneSnapshotNode& node)
        {
            if (node.camera) {
                return "camera";
            }
            if (node.renderable) {
                return "renderable";
            }
            return "node";
        }

        // Presence-only check for an optional component object on a node, used to
        // surface the removable component kinds (proximity/collision/motion) in
        // the snapshot's component list. We only need to know the member exists
        // and is an object — the editor offers "remove" without reading fields.
        bool has_component_object(
            const wz::json::JSONValue& obj,
            std::string_view key)
        {
            const auto* member = wz::json::find_member(obj, key);
            return member && member->kind == wz::json::JSONValueKind::Object;
        }

        // Render a behavior config scalar into the asset-graph param shape: a
        // kind token ("bool"/"int"/"float"/"string") plus a display value. The
        // authored JSON only distinguishes bool/number/string, so an integral
        // number is reported as "int" and a fractional one as "float".
        SceneSnapshotBehaviorConfig behavior_config_entry(
            const std::string& key,
            const wz::json::JSONValue& value)
        {
            SceneSnapshotBehaviorConfig out;
            out.name = key;
            switch (value.kind) {
            case wz::json::JSONValueKind::Bool:
                out.kind = "bool";
                out.value = value.bool_value ? "true" : "false";
                break;
            case wz::json::JSONValueKind::Number:
                out.kind =
                    value.number_value == std::floor(value.number_value)
                        ? "int"
                        : "float";
                out.value = format_scene_number(value.number_value);
                break;
            case wz::json::JSONValueKind::String:
                out.kind = "string";
                out.value = value.string_value;
                break;
            default:
                out.kind = "string";
                break;
            }
            return out;
        }

        SceneSnapshotBehavior read_behavior(const wz::json::JSONValue& obj)
        {
            SceneSnapshotBehavior behavior;
            behavior.id =
                std::string(wz::json::read_string(obj, "id").value_or(""));
            behavior.label =
                std::string(wz::json::read_string(obj, "label").value_or(""));
            behavior.module =
                std::string(wz::json::read_string(obj, "module").value_or(""));
            behavior.name =
                std::string(wz::json::read_string(obj, "name").value_or(""));
            behavior.enabled =
                wz::json::read_bool(obj, "enabled").value_or(true);

            if (const auto* events = wz::json::find_member(obj, "events");
                events && events->kind == wz::json::JSONValueKind::Array)
            {
                behavior.events.reserve(events->array_values.size());
                for (const auto& item : events->array_values) {
                    if (item
                        && item->kind == wz::json::JSONValueKind::String)
                    {
                        behavior.events.push_back(item->string_value);
                    }
                }
            }

            if (const auto* config = wz::json::find_member(obj, "config");
                config && config->kind == wz::json::JSONValueKind::Object)
            {
                behavior.config.reserve(config->object_members.size());
                for (const auto& member : config->object_members) {
                    if (member.value) {
                        behavior.config.push_back(
                            behavior_config_entry(member.key, *member.value));
                    }
                }
            }

            return behavior;
        }

        std::vector<SceneSnapshotBehavior> read_behaviors(
            const wz::json::JSONValue& obj)
        {
            std::vector<SceneSnapshotBehavior> behaviors;

            if (const auto* behavior =
                    wz::json::find_member(obj, "behavior");
                behavior && behavior->kind == wz::json::JSONValueKind::Object)
            {
                behaviors.push_back(read_behavior(*behavior));
            }

            if (const auto* plural =
                    wz::json::find_member(obj, "behaviors");
                plural && plural->kind == wz::json::JSONValueKind::Array)
            {
                behaviors.reserve(behaviors.size() + plural->array_values.size());
                for (const auto& item : plural->array_values) {
                    if (item
                        && item->kind == wz::json::JSONValueKind::Object)
                    {
                        behaviors.push_back(read_behavior(*item));
                    }
                }
            }

            return behaviors;
        }

        std::optional<FlatSceneSnapshotNode> read_node(
            const wz::json::JSONValue& value,
            std::string& error)
        {
            if (value.kind != wz::json::JSONValueKind::Object) {
                error = "scene nodes must be objects";
                return std::nullopt;
            }

            const auto id = wz::json::read_string(value, "id");
            if (!id || id->empty()) {
                error = "scene node is missing a required id";
                return std::nullopt;
            }

            SceneSnapshotNode node;
            node.id = std::string(*id);
            node.display_name =
                std::string(wz::json::read_string(value, "name").value_or(*id));
            if (const auto parent =
                    wz::json::read_string(value, "parent"))
            {
                node.parent_id = std::string(*parent);
            }
            node.visible = wz::json::read_bool(value, "visible").value_or(true);
            node.transform = read_transform(value);
            node.camera = read_camera(value);
            node.renderable = read_renderable(value);
            node.kind = node_kind(node);
            node.renderable_source = node.renderable
                ? node.renderable->source
                : SceneSnapshotRenderableSource{
                    .kind = "none",
                    .display_name = "none",
                };
            if (node.camera) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "camera",
                    .display_name = "Camera",
                });
            }
            // Surface the other optional, editor-removable components so the
            // editor can list them and offer "remove". Renderable stays surfaced
            // separately via renderable_source (above); camera stays the kind
            // token. These three are presence-only here — the editor detects them
            // through this component list (there is no per-component HasX flag).
            if (has_component_object(value, "proximity")) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "proximity",
                    .display_name = "Proximity",
                });
            }
            if (has_component_object(value, "collision")) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "collision",
                    .display_name = "Collision",
                });
            }
            if (has_component_object(value, "motion")) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "motion",
                    .display_name = "Motion",
                });
            }
            node.behaviors = read_behaviors(value);
            return FlatSceneSnapshotNode{ .node = std::move(node) };
        }

        SceneSnapshotNode shallow_node(const SceneSnapshotNode& node)
        {
            SceneSnapshotNode copy = node;
            copy.children.clear();
            return copy;
        }

        SceneSnapshotNode build_node(
            const std::vector<FlatSceneSnapshotNode>& entries,
            const std::unordered_map<std::string, std::vector<size_t>>&
                children_by_parent,
            size_t index,
            std::unordered_set<std::string>& ancestors)
        {
            const SceneSnapshotNode& source = entries[index].node;
            if (!ancestors.insert(source.id).second) {
                return shallow_node(source);
            }

            SceneSnapshotNode out = shallow_node(source);
            if (const auto it = children_by_parent.find(source.id);
                it != children_by_parent.end())
            {
                out.children.reserve(it->second.size());
                for (const size_t child : it->second) {
                    out.children.push_back(
                        build_node(
                            entries,
                            children_by_parent,
                            child,
                            ancestors));
                }
            }

            ancestors.erase(source.id);
            return out;
        }

        std::optional<SceneSnapshot> build_snapshot(
            const wz::json::JSONValue& root,
            std::string& error)
        {
            if (root.kind != wz::json::JSONValueKind::Object) {
                error = "scene file root must be an object";
                return std::nullopt;
            }

            SceneSnapshot snapshot;
            snapshot.schema =
                std::string(wz::json::read_string(root, "schema").value_or(""));
            if (snapshot.schema != "wozzits.scene.v0") {
                error = "scene JSON missing or unrecognized schema field";
                return std::nullopt;
            }
            snapshot.name =
                std::string(wz::json::read_string(root, "name").value_or(""));

            const auto* nodes = wz::json::find_member(root, "nodes");
            if (!nodes || nodes->kind != wz::json::JSONValueKind::Array) {
                error = "scene file is missing a nodes array";
                return std::nullopt;
            }

            std::vector<FlatSceneSnapshotNode> entries;
            entries.reserve(nodes->array_values.size());
            std::unordered_map<std::string, size_t> entries_by_id;
            for (const auto& item : nodes->array_values) {
                if (!item) {
                    continue;
                }
                auto entry = read_node(*item, error);
                if (!entry) {
                    return std::nullopt;
                }
                const std::string id = entry->node.id;
                if (!entries_by_id.emplace(id, entries.size()).second) {
                    error = "scene contains duplicate node id: " + id;
                    return std::nullopt;
                }
                entries.push_back(std::move(*entry));
            }

            std::unordered_map<std::string, std::vector<size_t>>
                children_by_parent;
            std::vector<size_t> roots;
            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& parent = entries[i].node.parent_id;
                if (parent && entries_by_id.contains(*parent)) {
                    children_by_parent[*parent].push_back(i);
                }
                else {
                    roots.push_back(i);
                }
            }
            if (roots.empty()) {
                roots.reserve(entries.size());
                for (size_t i = 0; i < entries.size(); ++i) {
                    roots.push_back(i);
                }
            }

            snapshot.roots.reserve(roots.size());
            std::unordered_set<std::string> ancestors;
            for (const size_t root_index : roots) {
                snapshot.roots.push_back(
                    build_node(
                        entries,
                        children_by_parent,
                        root_index,
                        ancestors));
            }

            return snapshot;
        }
    }

    SceneSnapshotLoadResult load_project_scene_snapshot(
        const wz::engine::project::ProjectManifestLoadDesc& desc)
    {
        SceneSnapshotLoadResult result;

        const auto loaded = wz::engine::project::load_project_manifest(desc);
        if (!loaded.ok) {
            result.error = loaded.error;
            return result;
        }
        if (loaded.manifest.scene_path.empty()) {
            result.error = "project manifest does not define a scene";
            return result;
        }

        const wz::fs::Path scene_path = resolve_resource_path(
            desc.resource_root,
            loaded.manifest.scene_path);
        const std::string scene_text = read_text_file(scene_path);
        if (scene_text.empty()) {
            result.error =
                "cannot read scene: " + loaded.manifest.scene_path;
            return result;
        }

        const wz::json::JSONParseResult parsed =
            wz::json::parse_json_string(scene_text);
        if (!parsed.ok || !parsed.document.root) {
            result.error = "invalid scene json";
            return result;
        }

        std::string error;
        auto snapshot = build_snapshot(*parsed.document.root, error);
        if (!snapshot) {
            result.error = error;
            return result;
        }

        result.snapshot = std::move(*snapshot);
        result.ok = true;
        return result;
    }
}
