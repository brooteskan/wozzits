#include <engine/behavior/behavior_registry.h>

#include <utility>

namespace wz::engine::behavior
{
    BehaviorHandle BehaviorRegistry::register_behavior(
        std::string module,
        std::string name,
        BehaviorFn function,
        void* user_data)
    {
        if (name.empty() || !function) {
            return {};
        }

        if (auto existing = find(module, name)) {
            registrations_[existing->index].function = function;
            registrations_[existing->index].user_data = user_data;
            return *existing;
        }

        const BehaviorHandle handle{
            .index = static_cast<uint32_t>(registrations_.size()),
        };
        registrations_.push_back({
            .module = std::move(module),
            .name = std::move(name),
            .function = function,
            .user_data = user_data,
        });
        return handle;
    }

    BehaviorHandle BehaviorRegistry::register_behavior(
        std::string name,
        BehaviorFn function,
        void* user_data)
    {
        return register_behavior({}, std::move(name), function, user_data);
    }

    BehaviorModuleHandle BehaviorRegistry::register_module(
        std::string module,
        BehaviorModuleEventFn on_event,
        std::vector<std::string> default_events,
        EventChannelMask default_channel_mask,
        void* user_data)
    {
        if (module.empty() || !on_event) {
            return {};
        }

        if (auto existing = find_module(module)) {
            modules_[existing->index].on_event = on_event;
            modules_[existing->index].default_events =
                std::move(default_events);
            modules_[existing->index].default_channel_mask =
                default_channel_mask;
            modules_[existing->index].user_data = user_data;
            return *existing;
        }

        const BehaviorModuleHandle handle{
            .index = static_cast<uint32_t>(modules_.size()),
        };
        modules_.push_back({
            .module = std::move(module),
            .on_event = on_event,
            .default_events = std::move(default_events),
            .default_channel_mask = default_channel_mask,
            .user_data = user_data,
        });
        return handle;
    }

    BehaviorModuleHandle BehaviorRegistry::register_module(
        std::string module,
        BehaviorModuleEventFn on_event,
        void* user_data)
    {
        return register_module(
            std::move(module),
            on_event,
            {},
            0u,
            user_data);
    }

    std::optional<BehaviorHandle> BehaviorRegistry::find(
        std::string_view module,
        std::string_view name) const noexcept
    {
        for (uint32_t i = 0; i < registrations_.size(); ++i) {
            const auto& registration = registrations_[i];
            if (registration.module == module
                && registration.name == name)
            {
                return BehaviorHandle{ .index = i };
            }
        }
        return std::nullopt;
    }

    const BehaviorRegistration* BehaviorRegistry::get(
        BehaviorHandle handle) const noexcept
    {
        if (!handle.valid() || handle.index >= registrations_.size()) {
            return nullptr;
        }
        return &registrations_[handle.index];
    }

    std::optional<BehaviorModuleHandle> BehaviorRegistry::find_module(
        std::string_view module) const noexcept
    {
        for (uint32_t i = 0; i < modules_.size(); ++i) {
            if (modules_[i].module == module) {
                return BehaviorModuleHandle{ .index = i };
            }
        }
        return std::nullopt;
    }

    const BehaviorModuleRegistration* BehaviorRegistry::get_module(
        BehaviorModuleHandle handle) const noexcept
    {
        if (!handle.valid() || handle.index >= modules_.size()) {
            return nullptr;
        }
        return &modules_[handle.index];
    }

    void BehaviorRegistry::clear()
    {
        registrations_.clear();
        modules_.clear();
    }
}
