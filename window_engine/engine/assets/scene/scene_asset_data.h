#pragma once

// engine/assets/scene/scene_asset_data.h

#include <asset/types.h>

#include <scene/scene_ecs.h>
#include <scene/transform_node.h>
#include <scene/compile/compiled_scene.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    // SceneAssetData is authored scene source data: a small scene description
    // language that compiles into SceneInstance and then into scene-render
    // storage. AssetKey references remain resource-DAG references; entity and
    // component composition lives here in the scene model.
    struct SceneRenderableBinding
    {
        wz::scene::SceneNodeClass node_class{};
        wz::scene::MeshHandle mesh{ wz::scene::INVALID_MESH };
        wz::scene::MaterialHandle material{ wz::scene::INVALID_MATERIAL };
        wz::scene::AABB local_bounds{};
        bool visible = true;
    };

    struct SceneLightAsset
    {
        wz::scene::AuthoredEntityId node_id;
        wz::scene::LightRecord light{};
    };

    struct SceneCameraAsset
    {
        float fov_y = 1.0472f;   // ~60 degrees
        float near_plane = 0.1f;
        float far_plane = 1000.0f;
        float aspect = 16.0f / 9.0f;
    };

    struct AuthoredTransform
    {
        float translation[3]{ 0.f, 0.f, 0.f };
        float rotation_quat[4]{ 0.f, 0.f, 0.f, 1.f };
        float scale[3]{ 1.f, 1.f, 1.f };
    };

    // ─── Non-render component descriptors ─────────────────────────────────
    // Data-only: parsed from scene JSON, instantiated into SceneInstance
    // component tables.  No runtime behavior is implemented here.

    // ─── Debug/editor visual descriptors ─────────────────────────────────

    // Auxiliary visuals are exportable authored visual helpers. The legacy
    // JSON field and compatibility aliases still use "debug_visual" for now.
    enum class SceneAuxiliaryVisualKind : uint8_t
    {
        None = 0,
        Axes,
    };

    struct SceneAuxiliaryVisualAsset
    {
        SceneAuxiliaryVisualKind kind = SceneAuxiliaryVisualKind::None;
        float scale = 1.0f;
        bool visible = true;
    };

    using SceneDebugVisualKind = SceneAuxiliaryVisualKind;
    using SceneDebugVisualAsset = SceneAuxiliaryVisualAsset;

    enum class SceneEditorHandleKind : uint8_t
    {
        None = 0,
        Translate,
        Rotate,
        Scale,
        Transform,
    };

    struct SceneEditorHandleAsset
    {
        SceneEditorHandleKind kind = SceneEditorHandleKind::Transform;
        bool enabled = true;
        bool visible = true;
        float size = 1.0f;
    };

    // ─────────────────────────────────────────────────────────────────────

    struct SceneInputReceiverAsset
    {
        std::string input_map;   // asset URI, e.g. "asset://input_maps/fly"
    };

    struct SceneFlyingCameraControllerAsset
    {
        float move_speed       = 5.0f;
        float look_speed       = 0.0005f;
        float boost_multiplier = 3.0f;
        float roll_speed       = 1.5f;
    };

    struct SceneAudioListenerAsset
    {
        bool active = true;
    };

    struct SceneEventListenerAsset
    {
        std::vector<std::string> channels;
    };

    // ─────────────────────────────────────────────────────────────────────

    struct SceneNodeAsset
    {
        wz::scene::AuthoredEntityId id;
        std::optional<wz::scene::AuthoredEntityId> parent_id;
        std::string name;

        AuthoredTransform local{};

        bool visible = true;

        wz::scene::TransformNode::MotionType motion_type =
            wz::scene::TransformNode::MotionType::Static;

        // Legacy embedded renderable authoring data, exported as
        // debug_renderable. Prefer renderable_asset for new authored scenes.
        std::optional<SceneRenderableBinding> renderable;

        // Preferred authored Renderable component. Scene JSON serializes this
        // as renderable.asset with a concrete asset-key string until symbolic
        // asset URI/name resolution exists.
        std::optional<wz::asset::AssetKey> renderable_asset;
        std::optional<SceneCameraAsset> camera;

        std::optional<SceneInputReceiverAsset> input_receiver;
        std::optional<SceneFlyingCameraControllerAsset> flying_camera_controller;
        std::optional<SceneAudioListenerAsset> audio_listener;
        std::optional<SceneEventListenerAsset> event_listener;

        std::optional<SceneAuxiliaryVisualAsset> debug_visual;
        std::optional<SceneEditorHandleAsset> editor_handle;
    };

    struct SceneDefaults
    {
        std::optional<wz::scene::AuthoredEntityId> active_camera_node;
    };

    struct SceneAssetData
    {
        std::string name;
        std::vector<SceneNodeAsset> nodes;
        std::vector<SceneLightAsset> lights;
        SceneDefaults defaults{};

        bool valid() const noexcept { return !nodes.empty(); }
    };

    inline bool has_authored_renderable_component(
        const SceneNodeAsset& node) noexcept
    {
        return node.renderable.has_value() || node.renderable_asset.has_value();
    }

    inline bool has_authored_camera_component(
        const SceneNodeAsset& node) noexcept
    {
        return node.camera.has_value();
    }

    inline bool has_authored_editor_only_components(
        const SceneNodeAsset& node) noexcept
    {
        return node.editor_handle.has_value();
    }

    inline bool has_authored_auxiliary_visual_component(
        const SceneNodeAsset& node) noexcept
    {
        return node.debug_visual.has_value();
    }

    inline bool has_authored_debug_visual_component(
        const SceneNodeAsset& node) noexcept
    {
        return has_authored_auxiliary_visual_component(node);
    }

    inline bool has_runtime_relevant_components(
        const SceneNodeAsset& node) noexcept
    {
        return has_authored_renderable_component(node)
            || has_authored_camera_component(node)
            || node.input_receiver.has_value()
            || node.flying_camera_controller.has_value()
            || node.audio_listener.has_value()
            || node.event_listener.has_value()
            || node.debug_visual.has_value();
    }

    inline wz::scene::SceneAuthoredComponentSummary summarize_authored_scene_components(
        const SceneAssetData& scene)
    {
        wz::scene::SceneAuthoredComponentSummary out{};
        out.nodes = static_cast<uint32_t>(scene.nodes.size());
        out.transforms = out.nodes;
        out.visibility = out.nodes;
        out.motion_types = out.nodes;
        out.lights = static_cast<uint32_t>(scene.lights.size());

        for (const auto& node : scene.nodes) {
            if (node.parent_id) {
                ++out.parent_links;
            }
            if (has_authored_renderable_component(node)) {
                ++out.renderables;
            }
            if (has_authored_camera_component(node)) {
                ++out.cameras;
            }
            if (node.input_receiver) {
                ++out.input_receivers;
            }
            if (node.flying_camera_controller) {
                ++out.flying_camera_controllers;
            }
            if (node.audio_listener) {
                ++out.audio_listeners;
            }
            if (node.event_listener) {
                ++out.event_listeners;
            }
            if (node.debug_visual) {
                ++out.auxiliary_visuals;
                ++out.debug_visuals;
            }
            if (node.editor_handle) {
                ++out.editor_handles;
            }
        }

        return out;
    }

} // namespace wz::engine::assets
