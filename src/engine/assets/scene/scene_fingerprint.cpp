#include <engine/assets/scene/scene_fingerprint.h>

#include <iomanip>
#include <sstream>
#include <string_view>

namespace wz::engine::assets
{
    namespace
    {
        struct SceneFingerprintBuilder
        {
            uint64_t value = 14695981039346656037ull;

            void mix_byte(uint8_t byte)
            {
                value ^= byte;
                value *= 1099511628211ull;
            }

            void mix_bytes(const void* data, std::size_t size)
            {
                const auto* bytes = static_cast<const uint8_t*>(data);
                for (std::size_t i = 0; i < size; ++i) {
                    mix_byte(bytes[i]);
                }
            }

            void mix_string(std::string_view text)
            {
                mix_bytes(text.data(), text.size());
                mix_byte(0xffu);
            }

            template <typename T>
            void mix_value(const T& value_in)
            {
                mix_bytes(&value_in, sizeof(value_in));
            }
        };
    }

    uint64_t scene_asset_fingerprint(const SceneAssetData& scene)
    {
        SceneFingerprintBuilder fp{};
        fp.mix_string(scene.name);
        fp.mix_value(scene.nodes.size());
        fp.mix_value(scene.lights.size());

        for (const auto& node : scene.nodes) {
            fp.mix_string(node.id);
            fp.mix_string(node.name);
            fp.mix_value(node.visible);
            fp.mix_value(node.motion_type);

            const bool has_parent = node.parent_id.has_value();
            fp.mix_value(has_parent);
            if (node.parent_id) {
                fp.mix_string(*node.parent_id);
            }

            fp.mix_bytes(node.local.translation, sizeof(node.local.translation));
            fp.mix_bytes(node.local.rotation_quat, sizeof(node.local.rotation_quat));
            fp.mix_bytes(node.local.scale, sizeof(node.local.scale));

            const bool has_inline_renderable = node.renderable.has_value();
            const bool has_renderable_asset = node.renderable_asset.has_value();
            const bool has_camera = node.camera.has_value();
            const bool has_debug_visual = node.debug_visual.has_value();
            const bool has_editor_handle = node.editor_handle.has_value();
            fp.mix_value(has_inline_renderable);
            fp.mix_value(has_renderable_asset);
            fp.mix_value(has_camera);
            fp.mix_value(has_debug_visual);
            fp.mix_value(has_editor_handle);

            if (node.renderable_asset) {
                const auto& key = *node.renderable_asset;
                fp.mix_value(key.content_hash.lo);
                fp.mix_value(key.content_hash.hi);
                fp.mix_value(key.schema_hash.lo);
                fp.mix_value(key.schema_hash.hi);
                fp.mix_value(key.compiler_hash.lo);
                fp.mix_value(key.compiler_hash.hi);
                fp.mix_value(key.deps_hash.lo);
                fp.mix_value(key.deps_hash.hi);
            }

            if (node.renderable) {
                const auto& renderable = *node.renderable;
                fp.mix_value(renderable.node_class);
                fp.mix_value(renderable.mesh);
                fp.mix_value(renderable.material);
                fp.mix_bytes(&renderable.local_bounds.min,
                    sizeof(renderable.local_bounds.min));
                fp.mix_bytes(&renderable.local_bounds.max,
                    sizeof(renderable.local_bounds.max));
                fp.mix_value(renderable.visible);
            }

            if (node.camera) {
                const auto& camera = *node.camera;
                fp.mix_value(camera.fov_y);
                fp.mix_value(camera.near_plane);
                fp.mix_value(camera.far_plane);
                fp.mix_value(camera.aspect);
            }

            if (node.input_receiver) {
                fp.mix_string(node.input_receiver->input_map);
                fp.mix_value(node.input_receiver->log_input);
            }

            if (node.flying_camera_controller) {
                const auto& controller = *node.flying_camera_controller;
                fp.mix_value(controller.move_speed);
                fp.mix_value(controller.look_speed);
                fp.mix_value(controller.boost_multiplier);
                fp.mix_value(controller.roll_speed);
            }

            if (node.audio_listener) {
                fp.mix_value(node.audio_listener->active);
            }

            if (node.event_listener) {
                fp.mix_value(node.event_listener->channels.size());
                for (const auto& channel : node.event_listener->channels) {
                    fp.mix_string(channel);
                }
            }

            if (node.debug_visual) {
                const auto& visual = *node.debug_visual;
                fp.mix_value(visual.kind);
                fp.mix_value(visual.scale);
                fp.mix_value(visual.visible);
            }

            if (node.editor_handle) {
                const auto& handle = *node.editor_handle;
                fp.mix_value(handle.kind);
                fp.mix_value(handle.enabled);
                fp.mix_value(handle.visible);
                fp.mix_value(handle.size);
            }
        }

        for (const auto& light : scene.lights) {
            fp.mix_string(light.node_id);
            fp.mix_value(light.light.position.x);
            fp.mix_value(light.light.position.y);
            fp.mix_value(light.light.position.z);
            fp.mix_value(light.light.direction.x);
            fp.mix_value(light.light.direction.y);
            fp.mix_value(light.light.direction.z);
            fp.mix_value(light.light.color.x);
            fp.mix_value(light.light.color.y);
            fp.mix_value(light.light.color.z);
            fp.mix_value(light.light.type);
            fp.mix_value(light.light.intensity);
            fp.mix_value(light.light.range);
        }

        const bool has_active_camera =
            scene.defaults.active_camera_node.has_value();
        fp.mix_value(has_active_camera);
        if (scene.defaults.active_camera_node) {
            fp.mix_string(*scene.defaults.active_camera_node);
        }

        return fp.value;
    }

    std::string scene_asset_fingerprint_string(const SceneAssetData& scene)
    {
        std::ostringstream out;
        out << "0x"
            << std::hex << std::setw(16) << std::setfill('0')
            << scene_asset_fingerprint(scene);
        return out.str();
    }
}
