#include <engine/editor/scene_snapshot.h>

#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <algorithm>
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

        // Read an authored asset-graph node ref of the form
        // { "<member>": { "asset_graph_node_id": N } } — the persisted shape of the
        // render-binding ingredients and the scene-source node ref (issue #213). The
        // resolved key is intentionally not persisted, so only the node id surfaces.
        std::optional<wz::asset::AssetGraphDraftNodeId> read_asset_graph_node_ref(
            const wz::json::JSONValue& obj,
            const char* member)
        {
            const auto* ref = wz::json::find_member(obj, member);
            if (!ref || ref->kind != wz::json::JSONValueKind::Object) {
                return std::nullopt;
            }
            const auto node_id =
                wz::json::read_number(*ref, "asset_graph_node_id");
            if (!node_id || *node_id < 0.0) {
                return std::nullopt;
            }
            return static_cast<wz::asset::AssetGraphDraftNodeId>(*node_id);
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

        // Convert an authored SceneBehaviorConfigValue (the running scene's
        // in-memory form) into the snapshot config shape -- the runtime-snapshot
        // analogue of behavior_config_entry, kept in lockstep with it so the
        // node-asset path reports the same kind tokens as the JSON path.
        SceneSnapshotBehaviorConfig snapshot_config_from_asset(
            const wz::engine::assets::SceneBehaviorConfigValue& value)
        {
            SceneSnapshotBehaviorConfig out;
            out.name = value.key;
            switch (value.kind) {
            case wz::engine::assets::SceneBehaviorConfigValueKind::Bool:
                out.kind = "bool";
                out.value = value.bool_value ? "true" : "false";
                break;
            case wz::engine::assets::SceneBehaviorConfigValueKind::Number:
                out.kind =
                    value.number_value == std::floor(value.number_value)
                        ? "int"
                        : "float";
                out.value = format_scene_number(value.number_value);
                break;
            case wz::engine::assets::SceneBehaviorConfigValueKind::String:
            default:
                out.kind = "string";
                out.value = value.string_value;
                break;
            }
            return out;
        }

        // Convert an authored SceneBehaviorAsset into the snapshot behavior shape
        // -- the runtime analogue of read_behavior, so a snapshot built from live
        // scene nodes (build_scene_snapshot_from_nodes) surfaces bindings the same
        // way the disk/JSON path does.
        SceneSnapshotBehavior snapshot_behavior_from_asset(
            const wz::engine::assets::SceneBehaviorAsset& source)
        {
            SceneSnapshotBehavior behavior;
            behavior.id = source.id;
            behavior.label = source.label;
            behavior.module = source.module;
            behavior.name = source.name;
            behavior.enabled = source.enabled;
            behavior.events = source.events;
            behavior.config.reserve(source.config.size());
            for (const auto& value : source.config) {
                behavior.config.push_back(snapshot_config_from_asset(value));
            }
            return behavior;
        }

        // Read the high-impact subset (surface/wireframe enabled + color) of a
        // MeshRenderStyleData JSON object (issue #213 Phase 3b-2). The shape is
        // written by scene_json_export.cpp::mesh_render_style_data_value: a
        // "surface"/"wireframe" object each with an "enabled" bool + a 4-element
        // "color" array. Tolerant: a missing layer/field leaves the default.
        SceneSnapshotMeshStyle read_mesh_style(const wz::json::JSONValue& style)
        {
            SceneSnapshotMeshStyle out;
            if (const auto* surface =
                    wz::json::find_member(style, "surface");
                surface && surface->kind == wz::json::JSONValueKind::Object)
            {
                out.surface_enabled =
                    wz::json::read_bool(*surface, "enabled").value_or(false);
                wz::json::read_float4(*surface, "color", out.surface_rgba);
            }
            if (const auto* wireframe =
                    wz::json::find_member(style, "wireframe");
                wireframe && wireframe->kind == wz::json::JSONValueKind::Object)
            {
                out.wireframe_enabled =
                    wz::json::read_bool(*wireframe, "enabled").value_or(false);
                wz::json::read_float4(*wireframe, "color", out.wireframe_rgba);
            }
            return out;
        }

        // Summarize a node's `glb_scene_source` descriptor (issue #213). Read-
        // only and tolerant: a missing/malformed block is treated as absent (we
        // return nullopt) rather than an error. Surfaces the summary — path,
        // consume_mode, scene_index, whether a base_style object exists, and the
        // count of style_overrides — plus the high-impact style subset (Phase
        // 3b-2): the base style (when present) and the per-mesh override table, so
        // the editor can pre-fill its per-component style editor.
        std::optional<SceneSnapshotSceneSource> read_scene_source(
            const wz::json::JSONValue& obj)
        {
            const auto* source = wz::json::find_member(obj, "glb_scene_source");
            if (!source || source->kind != wz::json::JSONValueKind::Object) {
                return std::nullopt;
            }

            SceneSnapshotSceneSource out;
            out.kind = "glb";
            out.path =
                std::string(wz::json::read_string(*source, "path").value_or(""));
            out.consume_mode = std::string(
                wz::json::read_string(*source, "consume_mode")
                    .value_or("instance"));
            out.scene_index =
                wz::json::read_uint(*source, "scene_index").value_or(0u);
            if (const auto* base = wz::json::find_member(*source, "base_style");
                base && base->kind == wz::json::JSONValueKind::Object)
            {
                out.has_base_style = true;
                out.base_style = read_mesh_style(*base);
            }
            if (const auto* overrides =
                    wz::json::find_member(*source, "style_overrides");
                overrides && overrides->kind == wz::json::JSONValueKind::Array)
            {
                out.style_override_count =
                    static_cast<uint32_t>(overrides->array_values.size());
                out.style_overrides.reserve(overrides->array_values.size());
                for (const auto& item : overrides->array_values) {
                    if (!item
                        || item->kind != wz::json::JSONValueKind::Object)
                    {
                        continue;
                    }
                    SceneSnapshotMeshStyleOverride ov;
                    ov.mesh_index =
                        wz::json::read_uint(*item, "mesh_index").value_or(0u);
                    if (const auto* st =
                            wz::json::find_member(*item, "style");
                        st && st->kind == wz::json::JSONValueKind::Object)
                    {
                        ov.style = read_mesh_style(*st);
                    }
                    out.style_overrides.push_back(std::move(ov));
                }
            }
            return out;
        }

        // Read the authored Collision-component field values from a node's
        // "collision" object (read-back gap fix). Tolerant: a missing/malformed
        // block is treated as absent (returns nullopt). Surfaces the editor-
        // authorable subset: the collision asset-graph node ref (the persisted
        // intent) and constrain_movement; these match scene_json_export's
        // collision_value writer.
        std::optional<SceneSnapshotCollision> read_collision(
            const wz::json::JSONValue& obj)
        {
            const auto* collision = wz::json::find_member(obj, "collision");
            if (!collision
                || collision->kind != wz::json::JSONValueKind::Object)
            {
                return std::nullopt;
            }
            SceneSnapshotCollision out;
            if (const auto node_id =
                    wz::json::read_number(*collision, "collision_asset_node_id");
                node_id && *node_id >= 0.0)
            {
                out.collision_asset_node_id =
                    static_cast<wz::asset::AssetGraphDraftNodeId>(*node_id);
            }
            out.constrain_movement =
                wz::json::read_bool(*collision, "constrain_movement")
                    .value_or(false);
            return out;
        }

        // Read the authored Motion-component terrain-stick field values from a
        // node's "motion" object (read-back gap fix). Tolerant: a missing block is
        // absent (nullopt). Field names match scene_json_export's motion_value.
        std::optional<SceneSnapshotMotion> read_motion(
            const wz::json::JSONValue& obj)
        {
            const auto* motion = wz::json::find_member(obj, "motion");
            if (!motion || motion->kind != wz::json::JSONValueKind::Object) {
                return std::nullopt;
            }
            SceneSnapshotMotion out;
            out.terrain_constrained =
                wz::json::read_bool(*motion, "terrain_constrained")
                    .value_or(false);
            out.ride_height = static_cast<float>(
                wz::json::read_number(*motion, "terrain_ride_height")
                    .value_or(0.0));
            out.footprint_radius = static_cast<float>(
                wz::json::read_number(*motion, "terrain_footprint_radius")
                    .value_or(0.0));
            out.align_to_surface =
                wz::json::read_bool(*motion, "terrain_align_to_surface")
                    .value_or(false);
            out.alignment_strength = static_cast<float>(
                wz::json::read_number(*motion, "terrain_alignment_strength")
                    .value_or(1.0));
            return out;
        }

        // Read the authored Motion-Filter-component field values from a node's
        // "motion_filter" object (read-back). Tolerant: a missing block is absent
        // (nullopt), missing fields keep defaults. Shape matches scene_json_
        // export's motion_filter_value.
        std::optional<SceneSnapshotMotionFilter> read_motion_filter(
            const wz::json::JSONValue& obj)
        {
            const auto* filter = wz::json::find_member(obj, "motion_filter");
            if (!filter || filter->kind != wz::json::JSONValueKind::Object) {
                return std::nullopt;
            }
            SceneSnapshotMotionFilter out;
            if (const auto* translation =
                    wz::json::find_member(*filter, "translation");
                translation
                && translation->kind == wz::json::JSONValueKind::Object)
            {
                const std::vector<double> smoothing =
                    read_number_array(*translation, "smoothing");
                for (std::size_t i = 0; i < 3u && i < smoothing.size(); ++i) {
                    out.translation_smoothing[i] =
                        static_cast<float>(smoothing[i]);
                }
                out.terrain_floor =
                    wz::json::read_bool(*translation, "terrain_floor")
                        .value_or(false);
                out.terrain_floor_offset = static_cast<float>(
                    wz::json::read_number(*translation, "terrain_floor_offset")
                        .value_or(0.0));
            }
            if (const auto* rotation =
                    wz::json::find_member(*filter, "rotation");
                rotation && rotation->kind == wz::json::JSONValueKind::Object)
            {
                const auto read_axis =
                    [&](const char* name,
                        SceneSnapshotMotionFilterRotationAxis& axis) {
                        const auto* a = wz::json::find_member(*rotation, name);
                        if (!a
                            || a->kind != wz::json::JSONValueKind::Object)
                        {
                            return;
                        }
                        axis.smoothing_time = static_cast<float>(
                            wz::json::read_number(*a, "smoothing_time")
                                .value_or(0.0));
                        axis.level =
                            wz::json::read_bool(*a, "level").value_or(false);
                        axis.limit =
                            wz::json::read_bool(*a, "limit").value_or(false);
                        axis.limit_min_degrees = static_cast<float>(
                            wz::json::read_number(*a, "limit_min_degrees")
                                .value_or(0.0));
                        axis.limit_max_degrees = static_cast<float>(
                            wz::json::read_number(*a, "limit_max_degrees")
                                .value_or(0.0));
                    };
                read_axis("roll", out.roll);
                read_axis("pitch", out.pitch);
                read_axis("yaw", out.yaw);
            }
            out.enabled = wz::json::read_bool(*filter, "enabled").value_or(true);
            return out;
        }

        // Read the authored AudioSource-component field values from a node's
        // "audio_source" object (read-back). Tolerant: a missing block is absent.
        // Field names match scene_json_export's audio_source export.
        std::optional<SceneSnapshotAudioSource> read_audio_source(
            const wz::json::JSONValue& obj)
        {
            const auto* source = wz::json::find_member(obj, "audio_source");
            if (!source || source->kind != wz::json::JSONValueKind::Object) {
                return std::nullopt;
            }
            SceneSnapshotAudioSource out;
            if (const auto node_id = wz::json::read_number(
                    *source, "audio_renderable_node_id");
                node_id && *node_id > 0.0)
            {
                out.audio_renderable_node_id =
                    static_cast<wz::asset::AssetGraphDraftNodeId>(*node_id);
            }
            out.auto_play =
                wz::json::read_bool(*source, "auto_play").value_or(true);
            out.enabled =
                wz::json::read_bool(*source, "enabled").value_or(true);
            return out;
        }

        // "atmosphere" object (read-back). Tolerant: a missing block is absent.
        // Field names match scene_json_export's atmosphere export (the authored
        // node id + enabled; the resolved atmosphere_asset key re-bridges on bind).
        std::optional<SceneSnapshotAtmosphere> read_atmosphere(
            const wz::json::JSONValue& obj)
        {
            const auto* source = wz::json::find_member(obj, "atmosphere");
            if (!source || source->kind != wz::json::JSONValueKind::Object) {
                return std::nullopt;
            }
            SceneSnapshotAtmosphere out;
            if (const auto node_id = wz::json::read_number(
                    *source, "atmosphere_asset_node_id");
                node_id && *node_id > 0.0)
            {
                out.atmosphere_asset_node_id =
                    static_cast<wz::asset::AssetGraphDraftNodeId>(*node_id);
            }
            out.enabled =
                wz::json::read_bool(*source, "enabled").value_or(true);
            return out;
        }

        // "environment" object (read-back). Tolerant: a missing block is absent.
        // Field names match scene_json_export's environment export (the authored
        // node id + enabled; the resolved environment_asset key re-bridges on bind).
        std::optional<SceneSnapshotEnvironment> read_environment(
            const wz::json::JSONValue& obj)
        {
            const auto* source = wz::json::find_member(obj, "environment");
            if (!source || source->kind != wz::json::JSONValueKind::Object) {
                return std::nullopt;
            }
            SceneSnapshotEnvironment out;
            if (const auto node_id = wz::json::read_number(
                    *source, "environment_asset_node_id");
                node_id && *node_id > 0.0)
            {
                out.environment_asset_node_id =
                    static_cast<wz::asset::AssetGraphDraftNodeId>(*node_id);
            }
            out.enabled =
                wz::json::read_bool(*source, "enabled").value_or(true);
            return out;
        }

        // "render_to_texture" object (read-back, issue #287). Tolerant: a missing
        // block is absent. Field names match scene_json_export's render_to_texture
        // export (the authored target node id + the switches; the resolved target
        // key re-bridges on bind). Note the exported id member is
        // "target_asset_node_id" while the in-memory field is target_node_id.
        std::optional<SceneSnapshotRenderToTexture> read_render_to_texture(
            const wz::json::JSONValue& obj)
        {
            const auto* source =
                wz::json::find_member(obj, "render_to_texture");
            if (!source || source->kind != wz::json::JSONValueKind::Object) {
                return std::nullopt;
            }
            SceneSnapshotRenderToTexture out;
            if (const auto node_id =
                    wz::json::read_number(*source, "target_asset_node_id");
                node_id && *node_id > 0.0)
            {
                out.target_node_id =
                    static_cast<wz::asset::AssetGraphDraftNodeId>(*node_id);
            }
            out.include_descendants =
                wz::json::read_bool(*source, "include_descendants")
                    .value_or(true);
            out.also_draw_in_scene =
                wz::json::read_bool(*source, "also_draw_in_scene")
                    .value_or(false);
            out.enabled =
                wz::json::read_bool(*source, "enabled").value_or(true);
            return out;
        }

        // Read the authored custom-renderable ingredients (issue #229/#230)
        // from a node's "renderable_bindings" / "renderable_constants" arrays.
        // Tolerant: missing blocks yield empty vectors; field names match
        // scene_json_export's writers (only the authored intent persists).
        std::vector<SceneSnapshotRenderableBinding> read_renderable_bindings(
            const wz::json::JSONValue& obj)
        {
            std::vector<SceneSnapshotRenderableBinding> out;
            const auto* bindings =
                wz::json::find_member(obj, "renderable_bindings");
            if (!bindings
                || bindings->kind != wz::json::JSONValueKind::Array)
            {
                return out;
            }
            for (const auto& entry_ptr : bindings->array_values) {
                if (!entry_ptr
                    || entry_ptr->kind != wz::json::JSONValueKind::Object)
                {
                    continue;
                }
                const auto semantic =
                    wz::json::read_string(*entry_ptr, "semantic");
                if (!semantic || semantic->empty()) {
                    continue;
                }
                SceneSnapshotRenderableBinding binding{};
                binding.semantic = std::string(*semantic);
                if (const auto anchor = wz::json::read_number(
                        *entry_ptr, "asset_graph_node_id");
                    anchor && *anchor > 0.0)
                {
                    binding.asset_graph_node_id =
                        static_cast<wz::asset::AssetGraphDraftNodeId>(*anchor);
                }
                out.push_back(std::move(binding));
            }
            return out;
        }

        std::vector<SceneSnapshotRenderableConstant> read_renderable_constants(
            const wz::json::JSONValue& obj)
        {
            std::vector<SceneSnapshotRenderableConstant> out;
            const auto* constants =
                wz::json::find_member(obj, "renderable_constants");
            if (!constants
                || constants->kind != wz::json::JSONValueKind::Array)
            {
                return out;
            }
            for (const auto& entry_ptr : constants->array_values) {
                if (!entry_ptr
                    || entry_ptr->kind != wz::json::JSONValueKind::Object)
                {
                    continue;
                }
                const auto name = wz::json::read_string(*entry_ptr, "name");
                if (!name || name->empty()) {
                    continue;
                }
                SceneSnapshotRenderableConstant constant{};
                constant.name = std::string(*name);
                wz::json::read_float4(*entry_ptr, "value", constant.value);
                out.push_back(std::move(constant));
            }
            return out;
        }

        // Map a live SceneNodeAsset's local AuthoredTransform (issue #213) into a
        // SceneSnapshotTransform, REUSING the same euler/display helpers the
        // file-path reader uses so the two routes format identically.
        SceneSnapshotTransform transform_from_authored(
            const wz::engine::assets::AuthoredTransform& local)
        {
            std::vector<double> translation{
                local.translation[0],
                local.translation[1],
                local.translation[2],
            };
            std::vector<double> rotation_quat{
                local.rotation_quat[0],
                local.rotation_quat[1],
                local.rotation_quat[2],
                local.rotation_quat[3],
            };
            std::vector<double> scale{
                local.scale[0],
                local.scale[1],
                local.scale[2],
            };
            std::vector<double> rotation_euler_degrees =
                rotation_euler_degrees_from_quat(rotation_quat);
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

        // Build a flat snapshot node from a live SceneNodeAsset (issue #213): the
        // projection of a runtime-grafted node — id/name/parent/visible/local
        // transform + renderable presence. It must mirror read_node's authored
        // components read-back (camera, collision, motion, motion filter, audio
        // source, behaviors, render-binding refs) so a scenelet-round-trip / spawned
        // node reveals + restores the same inspector sections the JSON path does;
        // each removable component appears in `components` (the section's gate) AND
        // carries its field values. Only derived/non-authorable state is omitted.
        FlatSceneSnapshotNode flat_node_from_asset(
            const wz::engine::assets::SceneNodeAsset& source)
        {
            SceneSnapshotNode node;
            node.id = source.id;
            node.display_name = source.name.empty() ? source.id : source.name;
            if (source.parent_id && !source.parent_id->empty()) {
                node.parent_id = *source.parent_id;
            }
            node.visible = source.visible;
            node.render_order = source.render_order;
            node.transform = transform_from_authored(source.local);

            // Surface renderable presence so the row reads as a renderable (a
            // grafted GLB child resolves a mesh-styled renderable that carries no
            // asset-graph node id, so leave asset_graph_node_id unset).
            const bool has_renderable =
                source.renderable_asset_node_id.has_value()
                || source.renderable_asset.has_value()
                || source.renderable.has_value();
            if (has_renderable) {
                SceneSnapshotRenderable renderable;
                renderable.asset_graph_node_id =
                    source.renderable_asset_node_id;
                renderable.source = renderable.asset_graph_node_id
                    ? SceneSnapshotRenderableSource{
                        .kind = "asset-graph-node",
                        .display_name = "asset-node",
                    }
                    : SceneSnapshotRenderableSource{
                        .kind = "scene-source",
                        .display_name = "instanced",
                    };
                node.renderable = renderable;
            }

            // Surface the authored render-binding refs so this path matches the
            // JSON node path (read_node): the render-program ref (a grafted
            // scene-source child whose program was re-applied from a host override,
            // issue #213) AND the geometry ref (a mesh assigned directly on the
            // node, #213 increment 2). Missing the geometry copy here made a
            // node's Geometry section vanish from the inspector after any
            // runtime-snapshot reload (e.g. a scenelet round trip) until the
            // editor restarted onto the JSON path.
            node.render_program_node_id = source.render_program_node_id;
            node.geometry_node_id = source.geometry_asset_node_id;

            // Custom-renderable ingredients (issue #229/#230): surface the
            // authored bindings + constant overrides so the inspector
            // pre-fills its rows, matching the JSON node path. Only the
            // authored intent (semantic + anchor, name + value) — the bridged
            // resolved keys are bind products, not authored state.
            node.renderable_bindings.reserve(
                source.renderable_bindings.size());
            for (const auto& binding : source.renderable_bindings) {
                node.renderable_bindings.push_back(
                    SceneSnapshotRenderableBinding{
                        .semantic = binding.semantic,
                        .asset_graph_node_id = binding.asset_graph_node_id,
                    });
            }
            node.renderable_constants.reserve(
                source.renderable_constants.size());
            for (const auto& constant : source.renderable_constants) {
                SceneSnapshotRenderableConstant out{};
                out.name = constant.name;
                std::copy(
                    constant.value, constant.value + 4, out.value);
                node.renderable_constants.push_back(std::move(out));
            }

            // Persisted Collision/Motion field values (read-back gap fix): the
            // grafted/SceneNodeAsset path reads them off the asset structs so the
            // inspector restores the same fields the JSON path surfaces.
            if (source.collision) {
                SceneSnapshotCollision collision;
                collision.collision_asset_node_id =
                    source.collision->collision_asset_node_id;
                collision.constrain_movement =
                    source.collision->constrain_movement;
                node.collision = collision;
            }
            if (source.motion) {
                node.motion = SceneSnapshotMotion{
                    .terrain_constrained = source.motion->terrain_constrained,
                    .ride_height = source.motion->terrain_ride_height,
                    .footprint_radius = source.motion->terrain_footprint_radius,
                    .align_to_surface = source.motion->terrain_align_to_surface,
                    .alignment_strength =
                        source.motion->terrain_alignment_strength,
                };
            }
            if (source.motion_filter) {
                const auto& f = *source.motion_filter;
                const auto axis =
                    [](const wz::engine::assets::SceneMotionFilterRotationAxis&
                            src) {
                        return SceneSnapshotMotionFilterRotationAxis{
                            .smoothing_time = src.smoothing_time,
                            .level = src.level,
                            .limit = src.limit,
                            .limit_min_degrees = src.limit_min_degrees,
                            .limit_max_degrees = src.limit_max_degrees,
                        };
                    };
                SceneSnapshotMotionFilter mf;
                mf.translation_smoothing[0] = f.translation_smoothing[0];
                mf.translation_smoothing[1] = f.translation_smoothing[1];
                mf.translation_smoothing[2] = f.translation_smoothing[2];
                mf.terrain_floor = f.terrain_floor;
                mf.terrain_floor_offset = f.terrain_floor_offset;
                mf.roll = axis(f.roll);
                mf.pitch = axis(f.pitch);
                mf.yaw = axis(f.yaw);
                mf.enabled = f.enabled;
                node.motion_filter = mf;
            }
            if (source.audio_source) {
                node.audio_source = SceneSnapshotAudioSource{
                    .audio_renderable_node_id =
                        source.audio_source->audio_renderable_node_id,
                    .auto_play = source.audio_source->auto_play,
                    .enabled = source.audio_source->enabled,
                };
            }
            if (source.atmosphere) {
                // atmosphere_asset_node_id is a plain id with 0 = unbound; the
                // snapshot carries an optional, so map 0 -> nullopt.
                std::optional<wz::asset::AssetGraphDraftNodeId> ref;
                if (source.atmosphere->atmosphere_asset_node_id != 0) {
                    ref = source.atmosphere->atmosphere_asset_node_id;
                }
                node.atmosphere = SceneSnapshotAtmosphere{
                    .atmosphere_asset_node_id = ref,
                    .enabled = source.atmosphere->enabled,
                };
            }
            if (source.environment) {
                // environment_asset_node_id is a plain id with 0 = unbound; the
                // snapshot carries an optional, so map 0 -> nullopt.
                std::optional<wz::asset::AssetGraphDraftNodeId> ref;
                if (source.environment->environment_asset_node_id != 0) {
                    ref = source.environment->environment_asset_node_id;
                }
                node.environment = SceneSnapshotEnvironment{
                    .environment_asset_node_id = ref,
                    .enabled = source.environment->enabled,
                };
            }
            if (source.render_to_texture) {
                // target_node_id is ALREADY an optional here (unlike atmosphere
                // and environment, which use 0 = unbound), so it carries over
                // directly.
                node.render_to_texture = SceneSnapshotRenderToTexture{
                    .target_node_id = source.render_to_texture->target_node_id,
                    .include_descendants =
                        source.render_to_texture->include_descendants,
                    .also_draw_in_scene =
                        source.render_to_texture->also_draw_in_scene,
                    .enabled = source.render_to_texture->enabled,
                };
            }

            // Surface behavior bindings. Without this, a tree rebuilt from the
            // running scene (e.g. after the prefab-editor open_scene round-trip)
            // shows "No behavior bindings" on every node even though they are
            // intact -- the JSON path fills these via read_behaviors, so mirror
            // it: the single `behavior` first, then the `behaviors` list.
            if (source.behavior) {
                node.behaviors.push_back(
                    snapshot_behavior_from_asset(*source.behavior));
            }
            node.behaviors.reserve(
                node.behaviors.size() + source.behaviors.size());
            for (const auto& behavior : source.behaviors) {
                node.behaviors.push_back(
                    snapshot_behavior_from_asset(behavior));
            }

            // Removable optional components (present iff the node carries them):
            // surface them so the inspector reveals + gates their sections after
            // an open_scene / grafted rebuild, matching read_node's list. Without
            // this the live path filled the field structs but the sections stayed
            // hidden (they gate on node.components), so a prefab re-open showed no
            // Motion/Collision/Motion Filter/Audio section and re-adding the
            // component overwrote the still-present saved fields with defaults.
            if (source.camera) {
                // Surface the camera field VALUES, not just the marker: the editor
                // reveals its Camera section on node.camera presence (HasCameraComponent
                // gates on node.Camera != null), so a marker alone left a grafted /
                // scenelet-round-trip camera node with no Camera section at all. Every
                // sibling component above copies its value struct; camera was the lone
                // omission -- mirror read_node, which fills node.camera.
                node.camera = SceneSnapshotCamera{
                    .fov_y = source.camera->fov_y,
                    .near_plane = source.camera->near_plane,
                    .far_plane = source.camera->far_plane,
                    .aspect = source.camera->aspect,
                };
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "camera", .display_name = "Camera" });
            }
            if (source.proximity) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "proximity", .display_name = "Proximity" });
            }
            if (source.collision) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "collision", .display_name = "Collision" });
            }
            if (source.motion) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "motion", .display_name = "Motion" });
            }
            if (source.motion_filter) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "motion_filter", .display_name = "Motion Filter" });
            }
            if (source.render_to_texture) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "render_to_texture",
                    .display_name = "Render To Texture" });
            }
            if (source.audio_source) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "audio_source", .display_name = "Audio Source" });
            }
            if (source.audio_listener) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "audio_listener",
                    .display_name = "Audio Listener" });
            }
            if (source.atmosphere) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "atmosphere", .display_name = "Atmosphere" });
            }
            if (source.environment) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "environment", .display_name = "Environment" });
            }

            node.kind = node_kind(node);
            node.renderable_source = node.renderable
                ? node.renderable->source
                : SceneSnapshotRenderableSource{
                    .kind = "none",
                    .display_name = "none",
                };
            return FlatSceneSnapshotNode{ .node = std::move(node) };
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
            node.render_order = static_cast<int>(
                wz::json::read_number(value, "render_order").value_or(0.0));
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
            if (has_component_object(value, "motion_filter")) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "motion_filter",
                    .display_name = "Motion Filter",
                });
            }
            if (has_component_object(value, "render_to_texture")) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "render_to_texture",
                    .display_name = "Render To Texture",
                });
            }
            if (has_component_object(value, "audio_source")) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "audio_source",
                    .display_name = "Audio Source",
                });
            }
            if (has_component_object(value, "audio_listener")) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "audio_listener",
                    .display_name = "Audio Listener",
                });
            }
            if (has_component_object(value, "atmosphere")) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "atmosphere",
                    .display_name = "Atmosphere",
                });
            }
            if (has_component_object(value, "environment")) {
                node.components.push_back(SceneSnapshotComponent{
                    .kind = "environment",
                    .display_name = "Environment",
                });
            }
            node.behaviors = read_behaviors(value);
            node.scene_source = read_scene_source(value);
            // Persisted Collision/Motion component field values (read-back gap
            // fix): surface so the inspector restores them on select + reload.
            node.collision = read_collision(value);
            node.motion = read_motion(value);
            node.motion_filter = read_motion_filter(value);
            node.audio_source = read_audio_source(value);
            node.atmosphere = read_atmosphere(value);
            node.environment = read_environment(value);
            node.render_to_texture = read_render_to_texture(value);
            // Authored render-binding refs (issue #213): surface the persisted
            // node ids so the inspector reveals + pre-selects these sections.
            node.scene_source_node_id =
                read_asset_graph_node_ref(value, "scene_source");
            node.geometry_node_id =
                read_asset_graph_node_ref(value, "geometry");
            node.render_program_node_id =
                read_asset_graph_node_ref(value, "render_program");
            // Custom-renderable ingredients (issue #229/#230): surface the
            // persisted bindings + constant overrides so the inspector
            // pre-fills its rows from the node's authored state.
            node.renderable_bindings = read_renderable_bindings(value);
            node.renderable_constants = read_renderable_constants(value);
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

    SceneSnapshot build_scene_snapshot_from_nodes(
        const std::vector<wz::engine::assets::SceneNodeAsset>& nodes)
    {
        SceneSnapshot snapshot;
        snapshot.schema = "wozzits.scene.v0";

        std::vector<FlatSceneSnapshotNode> entries;
        entries.reserve(nodes.size());
        std::unordered_map<std::string, size_t> entries_by_id;
        for (const wz::engine::assets::SceneNodeAsset& node : nodes) {
            if (node.id.empty()) {
                continue;  // no stable id to address the node by
            }
            if (!entries_by_id.emplace(node.id, entries.size()).second) {
                continue;  // ignore a duplicate id (keep the first)
            }
            entries.push_back(flat_node_from_asset(node));
        }

        // Parent/child assembly mirrors build_snapshot: a node whose parent is
        // present in the set nests; a node whose parent is NOT in the set is a
        // root but KEEPS its parent_id, which is how the editor finds the host to
        // graft this sub-tree under. (Unlike build_snapshot we never fall back to
        // "everything is a root" — an empty input simply yields no roots.)
        std::unordered_map<std::string, std::vector<size_t>> children_by_parent;
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

        snapshot.roots.reserve(roots.size());
        std::unordered_set<std::string> ancestors;
        for (const size_t root_index : roots) {
            snapshot.roots.push_back(
                build_node(entries, children_by_parent, root_index, ancestors));
        }

        return snapshot;
    }
}
