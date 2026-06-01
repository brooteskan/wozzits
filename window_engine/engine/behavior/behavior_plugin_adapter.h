#pragma once

// engine/behavior/behavior_plugin_adapter.h

#include <engine/behavior/behavior_plugin_abi.h>
#include <engine/behavior/behavior_registry.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace wz
{
    struct Logger;
}

namespace wz::engine::behavior
{
    class BehaviorPluginHost
    {
    public:
        struct Binding
        {
            WzBehaviorFn function = nullptr;
            WzBehaviorModuleEventFn on_event = nullptr;
            void* user_data = nullptr;
            wz::Logger* logger = nullptr;
        };

        enum class DynamicLoadStatus
        {
            Loaded,
            InvalidPath,
            LoadFailed,
            MissingRegisterSymbol,
            RegistrationFailed,
            UnsupportedPlatform,
        };

        struct DynamicLoadResult
        {
            DynamicLoadStatus status = DynamicLoadStatus::LoadFailed;
            std::string detail;

            bool ok() const noexcept
            {
                return status == DynamicLoadStatus::Loaded;
            }
        };

        struct DynamicModule
        {
            void* handle = nullptr;
            std::string path;
        };

        ~BehaviorPluginHost();

        bool register_static_pack(
            BehaviorRegistry& registry,
            WzRegisterBehaviorPluginFn register_plugin,
            wz::Logger* logger = nullptr,
            uint32_t api_version = WZ_BEHAVIOR_ABI_VERSION);

        DynamicLoadResult load_dynamic_module(
            BehaviorRegistry& registry,
            const std::filesystem::path& path,
            wz::Logger* logger = nullptr,
            const char* register_symbol =
                WZ_BEHAVIOR_PLUGIN_REGISTER_SYMBOL);

        uint32_t load_dynamic_modules_from_directory(
            BehaviorRegistry& registry,
            const std::filesystem::path& directory,
            wz::Logger* logger = nullptr);

        void clear();

        Binding* add_binding(
            WzBehaviorFn function,
            void* user_data,
            wz::Logger* logger);

        Binding* add_module_binding(
            WzBehaviorModuleEventFn on_event,
            void* user_data,
            wz::Logger* logger);

    private:
        std::vector<std::unique_ptr<Binding>> bindings_;
        std::vector<DynamicModule> dynamic_modules_;
    };
}
