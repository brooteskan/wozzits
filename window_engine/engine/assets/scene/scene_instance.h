#pragma once

// engine/assets/scene/scene_instance.h

#include <engine/assets/scene/scene_asset_data.h>

#include <scene/scene_graph.h>
#include <scene/compile/compiled_scene.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace wz::engine::assets
{
    // Runtime component record: associates a component with the runtime
    // node handle it was authored on.  Data-only — no behavior.
    template <typename T>
    struct SceneComponentRecord
    {
        wz::core::graph::NodeHandle node{};
        T component{};
    };

    // Runtime component types (instantiated from authored asset descriptors).
    struct InputReceiverComponent
    {
        std::string input_map;
    };

    struct FlyingCameraControllerComponent
    {
        float move_speed       = 5.0f;
        float look_speed       = 0.0005f;
        float boost_multiplier = 3.0f;
        float roll_speed       = 1.5f;
    };

    struct AudioListenerComponent
    {
        bool active = true;
    };

    struct EventListenerComponent
    {
        std::vector<std::string> channels;
    };

    struct DebugVisualComponent
    {
        SceneDebugVisualKind kind = SceneDebugVisualKind::None;
        float scale = 1.0f;
        bool visible = true;
    };

    struct SceneInstance
    {
        wz::scene::SceneStorage storage{};

        std::vector<wz::scene::RenderableDescriptor> renderables;
        std::vector<wz::scene::LightRecord> lights;

        wz::scene::ViewData default_view{};

        // Non-render component tables.
        std::vector<SceneComponentRecord<InputReceiverComponent>> input_receivers;
        std::vector<SceneComponentRecord<FlyingCameraControllerComponent>> flying_camera_controllers;
        std::vector<SceneComponentRecord<AudioListenerComponent>> audio_listeners;
        std::vector<SceneComponentRecord<EventListenerComponent>> event_listeners;
        std::vector<SceneComponentRecord<DebugVisualComponent>> debug_visuals;

        std::vector<std::string> runtime_to_authored;
        std::unordered_map<std::string, wz::core::graph::NodeHandle> authored_to_runtime;
    };

    enum class SceneInstantiateError
    {
        None = 0,
        DuplicateNodeId,
        ParentNotFound,
        ParentCycle,
        PolytreeBuildFailed,
    };

    struct SceneInstantiateResult
    {
        SceneInstance instance{};
        SceneInstantiateError error = SceneInstantiateError::None;
        std::string error_detail;

        bool ok() const noexcept { return error == SceneInstantiateError::None; }
    };

    SceneInstantiateResult instantiate_scene(const SceneAssetData& scene);

} // namespace wz::engine::assets
