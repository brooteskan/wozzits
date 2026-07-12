#pragma once

// engine/behavior/behavior_registry.h

#include <engine/behavior/behavior_commands.h>
#include <engine/behavior/behavior_gpu_compute.h>
#include <engine/behavior/behavior_spawn.h>
#include <engine/behavior/event_channels.h>
#include <engine/behavior/behavior_plugin_abi.h>
#include <engine/engine.h>

#include <scene/scene_ecs.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wz
{
    struct Logger;
}

namespace wz::engine
{
    struct FrameStorage;
}

namespace wz::engine::assets
{
    struct BehaviorComponent;
    struct BehaviorStateStorage;
    struct SceneInstance;
}

namespace wz::engine::behavior
{
    class BehaviorRegistry;

    struct BehaviorSurfaceRayQueryStats
    {
        uint32_t grid_queries = 0;
        uint32_t grid_cells_visited = 0;
        uint32_t grid_cells_rejected = 0;
        uint32_t triangle_bounds_tested = 0;
        uint32_t triangle_bounds_rejected = 0;
        uint32_t triangles_tested = 0;
    };

    struct BehaviorFrameContext
    {
        const wz::engine::FrameContext* frame_context = nullptr;
        const wz::engine::FrameStorage* frame_storage = nullptr;
        wz::engine::assets::SceneInstance* scene = nullptr;
        wz::engine::assets::BehaviorStateStorage* behavior_state = nullptr;
        const wz::engine::assets::BehaviorComponent* active_behavior = nullptr;
        wz::scene::RuntimeEntityId active_entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        const WzInputEventPayload* active_input_payload = nullptr;
        const WzGpuComputeEventPayload* active_gpu_compute_payload = nullptr;
        // Set (via ActiveSpawnPayloadScope) while dispatching a spawn-with-identity
        // completion event to the spawner, so the handler reads it through
        // facts.active_spawn_event (#252 pooling).
        const WzSpawnEventPayload* active_spawn_payload = nullptr;
        BehaviorCommandBuffer* commands = nullptr;
        BehaviorGpuComputeBuffer* gpu_compute = nullptr;
        // Spawn-with-identity sink (#252 pooling): submit_spawn_prefab enqueues here
        // mid-dispatch; the host drains it at the frame boundary. Null when the host
        // wires no identity-spawn path (submit_spawn_prefab then rejects).
        BehaviorSpawnBuffer* spawn_requests = nullptr;
        // Deferred runtime-authoring sink (#204): behaviors queue cheap live
        // scene-ECS authoring edits (spawn-child, remove-node, set-renderable)
        // here mid-dispatch; the runtime drains it at the frame boundary. Null
        // when the host wires no deferred-authoring path (the matching facts are
        // then inert).
        BehaviorAuthoringBuffer* authoring = nullptr;
        wz::Logger* logger = nullptr;
        BehaviorSurfaceRayQueryStats* surface_ray_stats = nullptr;
        // Absolute accumulated simulation time (seconds) for the self-paced
        // cognition.tick scheduler -- the monotonic clock dispatch_cognition_tick
        // compares scheduled wakes against, and the value surfaced to modules as
        // facts.sim_time. The host accumulates it per frame.
        double sim_time = 0.0;
        // The registry backing this dispatch, so facts can resolve behavior-
        // registered actuators by name (facts.actuator_lookup -- the open actuator
        // vocabulary). The dispatcher sets it next to `scene`; null in contexts that
        // never dispatch through the registry (some tests), where actuator lookups
        // simply miss and the statechart Call effect no-ops.
        const BehaviorRegistry* registry = nullptr;
    };

    using BehaviorFn = void (*)(
        BehaviorFrameContext& context,
        wz::scene::RuntimeEntityId entity,
        void* user_data);

    struct BehaviorEvent
    {
        WzBehaviorEventKind kind = WZ_EVENT_NONE;
        wz::scene::RuntimeEntityId entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        wz::scene::RuntimeEntityId other =
            wz::scene::INVALID_RUNTIME_ENTITY;
        bool self_is_trigger = false;
    };

    using BehaviorModuleEventFn = void (*)(
        BehaviorFrameContext& context,
        const BehaviorEvent& event,
        void* user_data);

    using BehaviorModuleInitFn = void (*)(
        BehaviorFrameContext& context,
        wz::scene::RuntimeEntityId entity,
        void* user_data);

    struct BehaviorHandle
    {
        uint32_t index = UINT32_MAX;

        bool valid() const noexcept { return index != UINT32_MAX; }
    };

    struct BehaviorModuleHandle
    {
        uint32_t index = UINT32_MAX;

        bool valid() const noexcept { return index != UINT32_MAX; }
    };

    // Declared tunable a behavior reads from its scene-component config. Mirrors
    // WzBehaviorParamDesc on the C ABI; the registry holds these so the host and
    // tools can enumerate a behavior's params with types + authoring defaults.
    enum class BehaviorParamType : uint8_t
    {
        None = 0,
        Float,
        Bool,
        String,
    };

    struct BehaviorParamSpec
    {
        std::string key;
        std::string label;
        BehaviorParamType type = BehaviorParamType::None;
        double default_number = 0.0;
        std::string default_string;
    };

    struct BehaviorRegistration
    {
        std::string module;
        std::string name;
        BehaviorFn function = nullptr;
        void* user_data = nullptr;
        std::vector<BehaviorParamSpec> params;
    };

    struct BehaviorModuleRegistration
    {
        std::string module;
        BehaviorModuleEventFn on_event = nullptr;
        BehaviorModuleInitFn on_init = nullptr;
        std::vector<std::string> default_events;
        EventChannelMask default_channel_mask = 0u;
        void* user_data = nullptr;
        std::vector<BehaviorParamSpec> params;  // declared config tunables
    };

    struct BehaviorGpuKernelPortContract
    {
        std::string name;
        WzGpuPortKind kind = WZ_GPU_PORT_NONE;
        WzGpuPortDirection direction = 0u;
        uint32_t stride_bytes = 0u;
    };

    struct BehaviorGpuKernelContract
    {
        std::string kernel_id;
        std::vector<BehaviorGpuKernelPortContract> ports;
    };

    // A registered actuator's parameter (mirrors WzActuatorParamDesc). Its authoring
    // kind drives which editor picker binds it and how the runner resolves the arg.
    enum class ActuatorParamKind : uint8_t
    {
        Scalar = 0,   // a chart const or a pure-op output
        Binding,      // a bound entity (a chart binding port)
        Agent,        // a chart agent's host entity
    };

    struct ActuatorParamSpec
    {
        std::string name;
        ActuatorParamKind kind = ActuatorParamKind::Scalar;
        double default_value = 0.0;
    };

    // A behavior-registered leaf actuator a statechart effect calls by name. The
    // registry holds the raw ABI fn pointer (invoked with the same facts the runner
    // is dispatching under) plus its declared param schema, so tools can enumerate it.
    struct BehaviorActuatorRegistration
    {
        std::string name;
        std::string label;
        std::vector<ActuatorParamSpec> params;
        WzBehaviorActuatorFn fn = nullptr;
        void* user_data = nullptr;
    };

    class BehaviorRegistry
    {
    public:
        BehaviorHandle register_behavior(
            std::string module,
            std::string name,
            BehaviorFn function,
            void* user_data = nullptr);

        BehaviorHandle register_behavior(
            std::string module,
            std::string name,
            BehaviorFn function,
            std::vector<BehaviorParamSpec> params,
            void* user_data = nullptr);

        BehaviorHandle register_behavior(
            std::string name,
            BehaviorFn function,
            void* user_data = nullptr);

        BehaviorModuleHandle register_module(
            std::string module,
            BehaviorModuleEventFn on_event,
            std::vector<std::string> default_events,
            EventChannelMask default_channel_mask,
            void* user_data = nullptr);

        BehaviorModuleHandle register_module(
            std::string module,
            BehaviorModuleEventFn on_event,
            BehaviorModuleInitFn on_init,
            std::vector<std::string> default_events,
            EventChannelMask default_channel_mask,
            void* user_data = nullptr);

        // As above, carrying the module's declared config params.
        BehaviorModuleHandle register_module(
            std::string module,
            BehaviorModuleEventFn on_event,
            BehaviorModuleInitFn on_init,
            std::vector<std::string> default_events,
            EventChannelMask default_channel_mask,
            std::vector<BehaviorParamSpec> params,
            void* user_data = nullptr);

        BehaviorModuleHandle register_module(
            std::string module,
            BehaviorModuleEventFn on_event,
            void* user_data = nullptr);

        bool register_gpu_kernel_contract(
            BehaviorGpuKernelContract contract);

        // Register a named actuator (the open statechart actuator vocabulary). A
        // later registration of the same name REPLACES the earlier one (last writer
        // wins), matching how a reloaded plugin re-registers its actuators. Returns
        // false on a null fn or empty name.
        bool register_actuator(BehaviorActuatorRegistration actuator);

        [[nodiscard]] const BehaviorActuatorRegistration* find_actuator(
            std::string_view name) const noexcept;

        [[nodiscard]] std::optional<BehaviorHandle> find(
            std::string_view module,
            std::string_view name) const noexcept;

        [[nodiscard]] const BehaviorRegistration* get(
            BehaviorHandle handle) const noexcept;

        [[nodiscard]] std::optional<BehaviorModuleHandle> find_module(
            std::string_view module) const noexcept;

        [[nodiscard]] const BehaviorModuleRegistration* get_module(
            BehaviorModuleHandle handle) const noexcept;

        [[nodiscard]] std::span<const BehaviorRegistration>
        registrations() const noexcept
        {
            return registrations_;
        }

        [[nodiscard]] std::span<const BehaviorModuleRegistration>
        modules() const noexcept
        {
            return modules_;
        }

        [[nodiscard]] std::span<const BehaviorGpuKernelContract>
        gpu_kernel_contracts() const noexcept
        {
            return gpu_kernel_contracts_;
        }

        [[nodiscard]] std::span<const BehaviorActuatorRegistration>
        actuators() const noexcept
        {
            return actuators_;
        }

        void clear();

    private:
        std::vector<BehaviorRegistration> registrations_;
        std::vector<BehaviorModuleRegistration> modules_;
        std::vector<BehaviorGpuKernelContract> gpu_kernel_contracts_;
        std::vector<BehaviorActuatorRegistration> actuators_;
    };
}
