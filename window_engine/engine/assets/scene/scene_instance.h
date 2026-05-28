#pragma once

// engine/assets/scene/scene_instance.h

#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/renderable/renderable.h>

#include <logging/logger.h>

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
    using SceneComponentRecord = wz::scene::RuntimeComponentRecord<T>;

    // Runtime component types (instantiated from authored asset descriptors).
    struct InputReceiverComponent
    {
        std::string input_map;
        bool log_input = false;
    };

    struct FlyingCameraControllerComponent
    {
        float move_speed       = 5.0f;
        float look_speed       = 0.0005f;
        float boost_multiplier = 3.0f;
        float roll_speed       = 1.5f;
    };

    struct ActorMovementControllerComponent
    {
        float move_speed = 5.0f;
        float boost_multiplier = 3.0f;
        SceneActorMovementSpace movement_space =
            SceneActorMovementSpace::World;
    };

    struct GroundBoundaryComponent
    {
        float min[3]{ 0.0f, 0.0f, 0.0f };
        float max[3]{ 0.0f, 0.0f, 0.0f };
        bool constrain_vertical = true;
        bool enabled = true;
    };

    struct AudioListenerComponent
    {
        bool active = true;
    };

    struct EventListenerComponent
    {
        std::vector<std::string> channels;
    };

    struct AuxiliaryVisualComponent
    {
        SceneAuxiliaryVisualKind kind = SceneAuxiliaryVisualKind::None;
        float scale = 1.0f;
        bool visible = true;
    };

    using DebugVisualComponent = AuxiliaryVisualComponent;

    struct EditorHandleComponent
    {
        SceneEditorHandleKind kind = SceneEditorHandleKind::Transform;
        bool enabled = true;
        bool visible = true;
        float size = 1.0f;
    };

    struct SceneInstance
    {
        // Runtime projection of authored SceneAssetData. instantiate_scene(...)
        // is the first compiler from authored scene language into this shape;
        // scene-render then compiles the graph, renderables, and lights into
        // render-oriented storage.
        wz::scene::SceneStorage storage{};

        std::vector<wz::scene::RenderableDescriptor> renderables;
        std::vector<wz::scene::LightRecord> lights;

        wz::scene::ViewData default_view{};

        // Non-render component tables.
        std::vector<SceneComponentRecord<InputReceiverComponent>> input_receivers;
        std::vector<SceneComponentRecord<FlyingCameraControllerComponent>> flying_camera_controllers;
        std::vector<SceneComponentRecord<ActorMovementControllerComponent>> actor_movement_controllers;
        std::vector<SceneComponentRecord<GroundBoundaryComponent>> ground_boundaries;
        std::vector<SceneComponentRecord<AudioListenerComponent>> audio_listeners;
        std::vector<SceneComponentRecord<EventListenerComponent>> event_listeners;
        std::vector<SceneComponentRecord<DebugVisualComponent>> debug_visuals;
        std::vector<SceneComponentRecord<EditorHandleComponent>> editor_handles;

        std::vector<wz::scene::AuthoredEntityId> runtime_to_authored;
        std::unordered_map<
            wz::scene::AuthoredEntityId,
            wz::scene::RuntimeEntityId> authored_to_runtime;
    };

    // ─── Renderable asset resolution ────────────────────────────────────

    wz::scene::SceneRuntimeComponentSummary summarize_scene_instance_components(
        const SceneInstance& instance);

    struct SceneRenderableResolver
    {
        virtual const RenderableAssetData* get(wz::asset::AssetKey key) const = 0;
        virtual ~SceneRenderableResolver() = default;
    };

    struct SceneRenderResourceResolver
    {
        virtual bool realize_renderable_descriptor(
            const RenderableAssetData& renderable,
            wz::scene::RenderableDescriptor& descriptor) const = 0;
        virtual ~SceneRenderResourceResolver() = default;
    };

    struct SceneInstantiateContext
    {
        const SceneRenderableResolver* renderable_resolver = nullptr;
        const SceneRenderResourceResolver* resource_resolver = nullptr;
        wz::Logger* logger = nullptr;
        const char* log_owner = nullptr;
    };

    // ─────────────────────────────────────────────────────────────────────

    enum class SceneInstantiateError
    {
        None = 0,
        DuplicateNodeId,
        ParentNotFound,
        ParentCycle,
        PolytreeBuildFailed,
        RenderableResolveFailed,
        RenderableRealizeFailed,
    };

    struct SceneInstantiateResult
    {
        SceneInstance instance{};
        SceneInstantiateError error = SceneInstantiateError::None;
        std::string error_detail;

        bool ok() const noexcept { return error == SceneInstantiateError::None; }
    };

    SceneInstantiateResult instantiate_scene(
        const SceneAssetData& scene,
        const SceneInstantiateContext& context = {});

    wz::math::Mat4 compose_scene_transform(const AuthoredTransform& transform);

    bool update_scene_asset_node_transform(
        SceneAssetData& asset,
        const SceneInstance& instance,
        wz::core::graph::NodeHandle node,
        const AuthoredTransform& local);

    bool update_scene_asset_node_transform(
        SceneAssetData& asset,
        const SceneInstance& instance,
        const std::string& authored_node_id,
        const AuthoredTransform& local);

} // namespace wz::engine::assets
