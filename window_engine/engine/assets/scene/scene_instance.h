#pragma once

// engine/assets/scene/scene_instance.h

#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/behavior/event_channels.h>

#include <logging/logger.h>

#include <scene/scene_graph.h>
#include <scene/compile/compiled_scene.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

    struct TerrainComponent
    {
        wz::asset::AssetKey terrain_asset{};
        wz::asset::AssetKey visual_proxy_asset{};
        wz::asset::AssetKey constraint_surface_asset{};
        bool visible = true;
        bool queryable = true;
        bool constrain_movement = true;
    };

    struct CollisionComponent
    {
        wz::asset::AssetKey collision_asset{};
        uint32_t layer_mask = 1;
        uint32_t collides_with_mask = 0xffffffffu;
        bool is_trigger = false;
        bool enabled = true;
        // When true, the collision acts as a terrain-style movement
        // constraint surface that Motion actors stick to.
        bool constrain_movement = false;
    };

    struct AudioListenerComponent
    {
        bool active = true;
    };

    struct AudioSourceComponent
    {
        wz::asset::AssetKey audio_renderable{};
        bool auto_play = true;
        bool enabled = true;

        // Stable per-entity voice tag, DERIVED at instantiate from the authored
        // node id (not authored, not on the renderable). The runtime plays this
        // source's voice(s) tagged with it, and a behavior addressing the same
        // entity computes/reads the same id to stop/retune them (audio-track
        // item 9). Never 0 (0 is the mixer's "untagged" sentinel).
        uint32_t client_id = 0;
    };

    struct EventListenerComponent
    {
        std::vector<std::string> channels;
        wz::engine::behavior::EventChannelMask channel_mask = 0u;
    };

    inline bool listener_accepts_event(
        const EventListenerComponent& listener,
        WzBehaviorEventKind kind) noexcept
    {
        return wz::engine::behavior::channel_mask_accepts_event(
            listener.channel_mask,
            kind);
    }

    struct ProximityComponent
    {
        float radius = 1.0f;
        uint32_t layer_mask = 1;
        uint32_t detects_with_mask = 0xffffffffu;
        bool enabled = true;
    };

    struct MotionComponent
    {
        float linear_velocity[3]{ 0.0f, 0.0f, 0.0f };
        float angular_velocity[3]{ 0.0f, 0.0f, 0.0f };
        SceneMotionSpace space = SceneMotionSpace::World;
        bool terrain_constrained = false;
        float terrain_ride_height = 0.0f;
        float terrain_footprint_radius = 0.0f;
        bool terrain_align_to_surface = false;
        float terrain_alignment_strength = 1.0f;
        // Runtime-only slew rate (radians/sec) for terrain alignment, set by a
        // behavior via SetTerrainAlignmentRate (not authored on SceneMotionAsset).
        // > 0 rate-limits how fast the up-axis swings toward the surface normal;
        // <= 0 uses the instantaneous strength path above. Re-asserted each frame
        // by the aligning behavior, so it survives a scene rebuild/spawn.
        float terrain_alignment_rate = 0.0f;
        bool enabled = true;
    };

    struct BehaviorComponent
    {
        std::string binding_id;
        std::string module;
        std::string name;
        bool enabled = true;
        std::vector<std::string> events;
        wz::engine::behavior::EventChannelMask channel_mask = 0u;
        std::vector<SceneBehaviorConfigValue> config;
    };

    inline uint32_t normalize_behavior_state_alignment(
        uint32_t alignment) noexcept
    {
        uint32_t normalized =
            alignment == 0u
            ? static_cast<uint32_t>(alignof(std::max_align_t))
            : alignment;
        normalized = std::max<uint32_t>(
            normalized,
            static_cast<uint32_t>(alignof(void*)));

        if ((normalized & (normalized - 1u)) == 0u) {
            return normalized;
        }

        uint32_t rounded = static_cast<uint32_t>(alignof(void*));
        while (rounded < normalized
            && rounded <= std::numeric_limits<uint32_t>::max() / 2u)
        {
            rounded *= 2u;
        }
        return rounded < normalized ? normalized : rounded;
    }

    struct BehaviorStateBlock
    {
        uint32_t size = 0;
        uint32_t alignment = 0;
        uint32_t layout_version = 0;
        void* data = nullptr;

        BehaviorStateBlock() = default;

        BehaviorStateBlock(const BehaviorStateBlock&) = delete;
        BehaviorStateBlock& operator=(const BehaviorStateBlock&) = delete;

        BehaviorStateBlock(BehaviorStateBlock&& other) noexcept
            : size(other.size)
            , alignment(other.alignment)
            , layout_version(other.layout_version)
            , data(other.data)
        {
            other.size = 0;
            other.alignment = 0;
            other.layout_version = 0;
            other.data = nullptr;
        }

        BehaviorStateBlock& operator=(BehaviorStateBlock&& other) noexcept
        {
            if (this != &other) {
                reset();
                size = other.size;
                alignment = other.alignment;
                layout_version = other.layout_version;
                data = other.data;
                other.size = 0;
                other.alignment = 0;
                other.layout_version = 0;
                other.data = nullptr;
            }
            return *this;
        }

        ~BehaviorStateBlock()
        {
            reset();
        }

        void reset() noexcept
        {
            if (data) {
                ::operator delete(
                    data,
                    std::align_val_t{
                        normalize_behavior_state_alignment(alignment),
                    });
            }
            size = 0;
            alignment = 0;
            layout_version = 0;
            data = nullptr;
        }

        bool allocate(
            uint32_t block_size,
            uint32_t block_alignment,
            uint32_t block_layout_version = 0)
        {
            reset();
            size = block_size;
            alignment =
                normalize_behavior_state_alignment(block_alignment);
            layout_version = block_layout_version;

            if (size == 0u) {
                return true;
            }

            data = ::operator new(
                size,
                std::align_val_t{ alignment },
                std::nothrow);
            if (!data) {
                reset();
                return false;
            }
            std::memset(data, 0, size);
            return true;
        }

        [[nodiscard]] bool compatible(
            uint32_t expected_size,
            uint32_t expected_alignment,
            uint32_t expected_layout_version = 0) const noexcept
        {
            return size == expected_size
                && alignment
                    == normalize_behavior_state_alignment(expected_alignment)
                && layout_version == expected_layout_version;
        }
    };

    struct BehaviorStateStorage
    {
        std::unordered_map<std::string, BehaviorStateBlock> instance_state;
        std::unordered_map<std::string, BehaviorStateBlock> shared_state;
        // Binding ids that have already received their one-shot WZ_EVENT_SELF_START.
        // Preserved across rebuild_behavior_scene (moved with the storage), so a
        // spawn fires self.start only for genuinely new bindings; existing actors
        // keep their started flag and are not re-notified.
        std::unordered_set<std::string> started_bindings;

        BehaviorStateBlock* find_instance_state(std::string_view binding_id)
        {
            const auto it = instance_state.find(std::string(binding_id));
            return it == instance_state.end() ? nullptr : &it->second;
        }

        const BehaviorStateBlock* find_instance_state(
            std::string_view binding_id) const
        {
            const auto it = instance_state.find(std::string(binding_id));
            return it == instance_state.end() ? nullptr : &it->second;
        }

        BehaviorStateBlock* find_shared_state(std::string_view key)
        {
            const auto it = shared_state.find(std::string(key));
            return it == shared_state.end() ? nullptr : &it->second;
        }

        const BehaviorStateBlock* find_shared_state(
            std::string_view key) const
        {
            const auto it = shared_state.find(std::string(key));
            return it == shared_state.end() ? nullptr : &it->second;
        }

        BehaviorStateBlock* allocate_instance_state(
            std::string binding_id,
            uint32_t size,
            uint32_t alignment,
            uint32_t layout_version = 0)
        {
            if (binding_id.empty()) {
                return nullptr;
            }
            auto& block = instance_state[std::move(binding_id)];
            if (block.compatible(size, alignment, layout_version)) {
                return &block;
            }
            return block.allocate(size, alignment, layout_version)
                ? &block
                : nullptr;
        }

        BehaviorStateBlock* allocate_shared_state(
            std::string key,
            uint32_t size,
            uint32_t alignment,
            uint32_t layout_version = 0)
        {
            if (key.empty()) {
                return nullptr;
            }
            auto& block = shared_state[std::move(key)];
            if (block.compatible(size, alignment, layout_version)) {
                return &block;
            }
            return block.allocate(size, alignment, layout_version)
                ? &block
                : nullptr;
        }
    };

    struct AuxiliaryVisualComponent
    {
        SceneAuxiliaryVisualKind kind = SceneAuxiliaryVisualKind::None;
        float scale = 1.0f;
        bool visible = true;
    };

    struct EditorHandleComponent
    {
        SceneEditorHandleKind kind = SceneEditorHandleKind::Transform;
        bool enabled = true;
        bool visible = true;
        float size = 1.0f;
    };

    struct MeshFieldVisualizationTargetComponent
    {
        wz::asset::AssetKey field_asset{};
        uint32_t channel_id = 0u;
    };

    enum class SceneAssetReferenceResolveStatus : uint8_t
    {
        Unassigned = 0,
        NotFound,
        TypeMismatch,
        Unavailable,
        Resolved,
    };

    struct SceneAssetReferenceResolveRecord
    {
        wz::scene::AuthoredEntityId node_id;
        std::string stable_asset_id;
        wz::asset::AssetType expected_type = wz::asset::AssetType::Unknown;
        wz::asset::AssetType resolved_type = wz::asset::AssetType::Unknown;
        wz::asset::AssetKey resolved_key{};
        SceneAssetReferenceResolveStatus status =
            SceneAssetReferenceResolveStatus::Unassigned;
        std::string detail;

        [[nodiscard]] bool resolved() const noexcept
        {
            return status == SceneAssetReferenceResolveStatus::Resolved;
        }
    };

    struct SceneInstance
    {
        // Runtime projection of authored SceneAssetData. instantiate_scene(...)
        // is the first compiler from authored scene language into this shape;
        // scene-render then compiles the graph, renderables, and lights into
        // render-oriented storage.
        wz::scene::SceneStorage storage{};

        std::vector<wz::scene::RenderableDescriptor> renderables;
        uint32_t cameras = 0;
        std::vector<wz::scene::LightRecord> lights;
        uint32_t hdri_environments = 0;
        std::vector<SceneSkyDrawAsset> sky_draws;

        wz::scene::ViewData default_view{};

        // Non-render component tables.
        std::vector<SceneComponentRecord<InputReceiverComponent>> input_receivers;
        std::vector<SceneComponentRecord<FlyingCameraControllerComponent>> flying_camera_controllers;
        std::vector<SceneComponentRecord<ActorMovementControllerComponent>> actor_movement_controllers;
        std::vector<SceneComponentRecord<GroundBoundaryComponent>> ground_boundaries;
        std::vector<SceneComponentRecord<CollisionComponent>> collisions;
        std::vector<SceneComponentRecord<TerrainComponent>> terrains;
        std::vector<SceneComponentRecord<AudioListenerComponent>> audio_listeners;
        std::vector<SceneComponentRecord<AudioSourceComponent>> audio_sources;
        std::vector<SceneComponentRecord<EventListenerComponent>> event_listeners;
        std::vector<SceneComponentRecord<ProximityComponent>> proximities;
        std::vector<SceneComponentRecord<MotionComponent>> motions;
        std::vector<SceneComponentRecord<BehaviorComponent>> behaviors;
        uint32_t compute_kernels = 0;
        uint32_t render_shaders = 0;
        BehaviorStateStorage behavior_state;
        std::vector<SceneComponentRecord<AuxiliaryVisualComponent>> auxiliary_visuals;
        std::vector<SceneComponentRecord<EditorHandleComponent>> editor_handles;
        std::vector<SceneComponentRecord<MeshFieldVisualizationTargetComponent>>
            mesh_field_visualization_targets;
        std::vector<SceneAssetReferenceResolveRecord>
            asset_reference_resolutions;

        std::vector<wz::scene::AuthoredEntityId> runtime_to_authored;
        std::vector<std::string> runtime_names;
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
        DuplicateBehaviorBindingId,
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
