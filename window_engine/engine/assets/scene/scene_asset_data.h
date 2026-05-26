#pragma once

// engine/assets/scene/scene_asset_data.h

#include <scene/transform_node.h>
#include <scene/compile/compiled_scene.h>

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
        std::optional<SceneCameraAsset> camera;
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
