#include <engine/assets/scene/scene_json_export.h>

#include <cstddef>
#include <memory>
#include <utility>

namespace wz::engine::assets
{
    namespace
    {
        using wz::json::JSONMember;
        using wz::json::JSONValue;
        using wz::json::JSONValueKind;
        using wz::json::JSONValuePtr;

        JSONValuePtr value(JSONValueKind kind)
        {
            auto out = std::make_unique<JSONValue>();
            out->kind = kind;
            return out;
        }

        JSONValuePtr null_value()
        {
            return value(JSONValueKind::Null);
        }

        JSONValuePtr bool_value(bool v)
        {
            auto out = value(JSONValueKind::Bool);
            out->bool_value = v;
            return out;
        }

        JSONValuePtr number_value(double v)
        {
            auto out = value(JSONValueKind::Number);
            out->number_value = v;
            return out;
        }

        JSONValuePtr string_value(const std::string& v)
        {
            auto out = value(JSONValueKind::String);
            out->string_value = v;
            return out;
        }

        JSONValuePtr string_value(const char* v)
        {
            auto out = value(JSONValueKind::String);
            out->string_value = v;
            return out;
        }

        JSONValuePtr array_value()
        {
            return value(JSONValueKind::Array);
        }

        JSONValuePtr object_value()
        {
            return value(JSONValueKind::Object);
        }

        void add_member(
            JSONValue& object,
            std::string key,
            JSONValuePtr member_value)
        {
            object.object_members.push_back(JSONMember{
                .key = std::move(key),
                .value = std::move(member_value),
            });
        }

        JSONValuePtr float_array(const float* values, size_t count)
        {
            auto arr = array_value();
            for (size_t i = 0; i < count; ++i) {
                arr->array_values.push_back(number_value(values[i]));
            }
            return arr;
        }

        const char* debug_visual_kind_name(SceneDebugVisualKind kind)
        {
            switch (kind) {
            case SceneDebugVisualKind::Axes: return "axes";
            case SceneDebugVisualKind::None:
            default: return "";
            }
        }

        const char* editor_handle_kind_name(SceneEditorHandleKind kind)
        {
            switch (kind) {
            case SceneEditorHandleKind::Translate: return "translate";
            case SceneEditorHandleKind::Rotate: return "rotate";
            case SceneEditorHandleKind::Scale: return "scale";
            case SceneEditorHandleKind::Transform: return "transform";
            case SceneEditorHandleKind::None:
            default: return "";
            }
        }

        const char* pipeline_name(const SceneRenderableBinding& binding)
        {
            if (binding.node_class.default_surface
                == wz::scene::SurfaceClass::Transparent)
            {
                return "TransparentGeometry";
            }
            return "OpaqueGeometry";
        }

        JSONValuePtr transform_value(const AuthoredTransform& transform)
        {
            auto obj = object_value();
            add_member(*obj, "translation",
                float_array(transform.translation, 3));
            add_member(*obj, "rotation_quat",
                float_array(transform.rotation_quat, 4));
            add_member(*obj, "scale",
                float_array(transform.scale, 3));
            return obj;
        }

        JSONValuePtr renderable_value(const SceneRenderableBinding& binding)
        {
            float bounds_min[3]{
                binding.local_bounds.min.x,
                binding.local_bounds.min.y,
                binding.local_bounds.min.z,
            };
            float bounds_max[3]{
                binding.local_bounds.max.x,
                binding.local_bounds.max.y,
                binding.local_bounds.max.z,
            };

            auto obj = object_value();
            add_member(*obj, "pipeline", string_value(pipeline_name(binding)));
            add_member(*obj, "mesh", number_value(binding.mesh));
            add_member(*obj, "material", number_value(binding.material));

            auto bounds = object_value();
            add_member(*bounds, "min", float_array(bounds_min, 3));
            add_member(*bounds, "max", float_array(bounds_max, 3));
            add_member(*obj, "bounds", std::move(bounds));

            add_member(*obj, "visible", bool_value(binding.visible));
            return obj;
        }

        JSONValuePtr camera_value(const SceneCameraAsset& camera)
        {
            auto obj = object_value();
            add_member(*obj, "fov_y", number_value(camera.fov_y));
            add_member(*obj, "near", number_value(camera.near_plane));
            add_member(*obj, "far", number_value(camera.far_plane));
            add_member(*obj, "aspect", number_value(camera.aspect));
            return obj;
        }

        JSONValuePtr flying_camera_value(
            const SceneFlyingCameraControllerAsset& controller)
        {
            auto obj = object_value();
            add_member(*obj, "move_speed",
                number_value(controller.move_speed));
            add_member(*obj, "look_speed",
                number_value(controller.look_speed));
            add_member(*obj, "boost_multiplier",
                number_value(controller.boost_multiplier));
            add_member(*obj, "roll_speed",
                number_value(controller.roll_speed));
            return obj;
        }

        JSONValuePtr event_listener_value(
            const SceneEventListenerAsset& listener)
        {
            auto obj = object_value();
            auto channels = array_value();
            for (const auto& channel : listener.channels) {
                channels->array_values.push_back(string_value(channel));
            }
            add_member(*obj, "channels", std::move(channels));
            return obj;
        }

        JSONValuePtr debug_visual_value(const SceneDebugVisualAsset& visual)
        {
            auto obj = object_value();
            add_member(*obj, "kind",
                string_value(debug_visual_kind_name(visual.kind)));
            add_member(*obj, "scale", number_value(visual.scale));
            add_member(*obj, "visible", bool_value(visual.visible));
            return obj;
        }

        JSONValuePtr editor_handle_value(const SceneEditorHandleAsset& handle)
        {
            auto obj = object_value();
            add_member(*obj, "kind",
                string_value(editor_handle_kind_name(handle.kind)));
            add_member(*obj, "enabled", bool_value(handle.enabled));
            add_member(*obj, "visible", bool_value(handle.visible));
            add_member(*obj, "size", number_value(handle.size));
            return obj;
        }

        JSONValuePtr node_value(const SceneNodeAsset& node)
        {
            auto obj = object_value();
            add_member(*obj, "id", string_value(node.id));
            if (node.parent_id) {
                add_member(*obj, "parent", string_value(*node.parent_id));
            }
            else {
                add_member(*obj, "parent", null_value());
            }
            if (!node.name.empty() && node.name != node.id) {
                add_member(*obj, "name", string_value(node.name));
            }
            add_member(*obj, "transform", transform_value(node.local));
            add_member(*obj, "visible", bool_value(node.visible));
            if (node.motion_type
                == wz::scene::TransformNode::MotionType::Animated)
            {
                add_member(*obj, "motion_type", string_value("Animated"));
            }
            if (node.renderable) {
                add_member(*obj, "debug_renderable",
                    renderable_value(*node.renderable));
            }
            if (node.camera) {
                add_member(*obj, "camera", camera_value(*node.camera));
            }
            if (node.input_receiver) {
                auto input = object_value();
                add_member(*input, "input_map",
                    string_value(node.input_receiver->input_map));
                add_member(*obj, "input_receiver", std::move(input));
            }
            if (node.flying_camera_controller) {
                add_member(*obj, "flying_camera_controller",
                    flying_camera_value(*node.flying_camera_controller));
            }
            if (node.audio_listener) {
                auto audio = object_value();
                add_member(*audio, "active",
                    bool_value(node.audio_listener->active));
                add_member(*obj, "audio_listener", std::move(audio));
            }
            if (node.event_listener) {
                add_member(*obj, "event_listener",
                    event_listener_value(*node.event_listener));
            }
            if (node.debug_visual
                && node.debug_visual->kind != SceneDebugVisualKind::None)
            {
                add_member(*obj, "debug_visual",
                    debug_visual_value(*node.debug_visual));
            }
            if (node.editor_handle
                && node.editor_handle->kind != SceneEditorHandleKind::None)
            {
                add_member(*obj, "editor_handle",
                    editor_handle_value(*node.editor_handle));
            }
            return obj;
        }

        JSONValuePtr light_value(const SceneLightAsset& light)
        {
            float pos[3]{
                light.light.position.x,
                light.light.position.y,
                light.light.position.z,
            };
            float dir[3]{
                light.light.direction.x,
                light.light.direction.y,
                light.light.direction.z,
            };
            float color[3]{
                light.light.color.x,
                light.light.color.y,
                light.light.color.z,
            };

            auto obj = object_value();
            add_member(*obj, "node_id", string_value(light.node_id));

            auto record = object_value();
            add_member(*record, "position", float_array(pos, 3));
            add_member(*record, "direction", float_array(dir, 3));
            add_member(*record, "color", float_array(color, 3));
            add_member(*record, "intensity",
                number_value(light.light.intensity));
            add_member(*record, "range", number_value(light.light.range));

            add_member(*obj, "light", std::move(record));
            return obj;
        }
    }

    wz::json::JSONDocument export_scene_to_json_document(
        const SceneAssetData& scene)
    {
        wz::json::JSONDocument document{};
        auto root = object_value();

        add_member(*root, "schema", string_value("wozzits.scene.v0"));
        add_member(*root, "name", string_value(scene.name));

        auto nodes = array_value();
        for (const auto& node : scene.nodes) {
            nodes->array_values.push_back(node_value(node));
        }
        add_member(*root, "nodes", std::move(nodes));

        if (!scene.lights.empty()) {
            auto lights = array_value();
            for (const auto& light : scene.lights) {
                lights->array_values.push_back(light_value(light));
            }
            add_member(*root, "lights", std::move(lights));
        }

        if (scene.defaults.active_camera_node) {
            auto defaults = object_value();
            add_member(*defaults, "active_camera",
                string_value(*scene.defaults.active_camera_node));
            add_member(*root, "defaults", std::move(defaults));
        }

        document.root = std::move(root);
        return document;
    }

} // namespace wz::engine::assets
