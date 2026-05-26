#pragma once

// engine/assets/scene/scene_asset_data.h

#include <asset/types.h>

#include <scene/transform_node.h>
#include <scene/compile/compiled_scene.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wz::engine::assets
{
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
        std::string node_id;
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

    enum class SceneDebugVisualKind : uint8_t
    {
        None = 0,
        Axes,
    };

    struct SceneDebugVisualAsset
    {
        SceneDebugVisualKind kind = SceneDebugVisualKind::None;
        float scale = 1.0f;
        bool visible = true;
    };

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
        std::string id;
        std::optional<std::string> parent_id;
        std::string name;

        AuthoredTransform local{};

        bool visible = true;

        wz::scene::TransformNode::MotionType motion_type =
            wz::scene::TransformNode::MotionType::Static;

        std::optional<SceneRenderableBinding> renderable;
        std::optional<wz::asset::AssetKey> renderable_asset;
        std::optional<SceneCameraAsset> camera;

        std::optional<SceneInputReceiverAsset> input_receiver;
        std::optional<SceneFlyingCameraControllerAsset> flying_camera_controller;
        std::optional<SceneAudioListenerAsset> audio_listener;
        std::optional<SceneEventListenerAsset> event_listener;

        std::optional<SceneDebugVisualAsset> debug_visual;
        std::optional<SceneEditorHandleAsset> editor_handle;
    };

    struct SceneDefaults
    {
        std::optional<std::string> active_camera_node;
    };

    struct SceneAssetData
    {
        std::string name;
        std::vector<SceneNodeAsset> nodes;
        std::vector<SceneLightAsset> lights;
        SceneDefaults defaults{};

        bool valid() const noexcept { return !nodes.empty(); }
    };

} // namespace wz::engine::assets
