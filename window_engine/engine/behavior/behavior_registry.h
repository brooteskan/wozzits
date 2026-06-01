#pragma once

// engine/behavior/behavior_registry.h

#include <engine/behavior/behavior_commands.h>
#include <engine/behavior/behavior_plugin_abi.h>
#include <engine/engine.h>

#include <scene/scene_ecs.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wz::engine
{
    struct FrameStorage;
}

namespace wz::engine::assets
{
    struct SceneInstance;
}

namespace wz::engine::behavior
{
    struct BehaviorFrameContext
    {
        const wz::engine::FrameContext* frame_context = nullptr;
        const wz::engine::FrameStorage* frame_storage = nullptr;
        const wz::engine::assets::SceneInstance* scene = nullptr;
        BehaviorCommandBuffer* commands = nullptr;
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

    struct BehaviorRegistration
    {
        std::string module;
        std::string name;
        BehaviorFn function = nullptr;
        void* user_data = nullptr;
    };

    struct BehaviorModuleRegistration
    {
        std::string module;
        BehaviorModuleEventFn on_event = nullptr;
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
            std::string name,
            BehaviorFn function,
            void* user_data = nullptr);

        BehaviorModuleHandle register_module(
            std::string module,
            BehaviorModuleEventFn on_event,
            void* user_data = nullptr);

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

        void clear();

    private:
        std::vector<BehaviorRegistration> registrations_;
        std::vector<BehaviorModuleRegistration> modules_;
    };
}
