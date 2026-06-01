#include <engine/behavior/behavior_plugin_adapter.h>

#include <engine/collision/collision_frame.h>
#include <engine/frame_storage.h>

#include <input/input.h>
#include <logging/logger.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <algorithm>
#include <filesystem>
#include <string>

namespace wz::engine::behavior
{
    namespace
    {
        struct RegisterContext
        {
            BehaviorRegistry* registry = nullptr;
            BehaviorPluginHost* host = nullptr;
            wz::Logger* logger = nullptr;
        };

        const char* dynamic_load_status_name(
            BehaviorPluginHost::DynamicLoadStatus status) noexcept
        {
            using Status = BehaviorPluginHost::DynamicLoadStatus;
            switch (status) {
            case Status::Loaded:
                return "loaded";
            case Status::InvalidPath:
                return "invalid_path";
            case Status::LoadFailed:
                return "load_failed";
            case Status::MissingRegisterSymbol:
                return "missing_register_symbol";
            case Status::RegistrationFailed:
                return "registration_failed";
            case Status::UnsupportedPlatform:
                return "unsupported_platform";
            }
            return "unknown";
        }

        WzCollisionEventKind to_abi_collision_kind(
            wz::engine::collision::CollisionEventKind kind) noexcept
        {
            using Kind = wz::engine::collision::CollisionEventKind;
            switch (kind) {
            case Kind::Enter:
                return WZ_COLLISION_EVENT_ENTER;
            case Kind::Stay:
                return WZ_COLLISION_EVENT_STAY;
            case Kind::Exit:
                return WZ_COLLISION_EVENT_EXIT;
            }
            return 0u;
        }

        WzBehaviorEventKind to_abi_behavior_event_kind(
            const BehaviorEvent& event) noexcept
        {
            return event.kind;
        }

        BehaviorCommandKind from_abi_command_kind(
            WzBehaviorCommandKind kind) noexcept
        {
            switch (kind) {
            case WZ_BEHAVIOR_COMMAND_ADD_LOCAL_TRANSLATION:
                return BehaviorCommandKind::AddLocalTranslation;
            case WZ_BEHAVIOR_COMMAND_SET_LOCAL_TRANSLATION:
                return BehaviorCommandKind::SetLocalTranslation;
            case WZ_BEHAVIOR_COMMAND_NONE:
            default:
                return BehaviorCommandKind::None;
            }
        }

        void fill_input_view(
            const wz::input::InputState& input,
            WzInputStateView& out) noexcept
        {
            for (uint32_t i = 0; i < 256; ++i) {
                out.keyboard_down[i] =
                    input.keyboard.down[i] ? uint8_t{ 1 } : uint8_t{ 0 };
                out.keyboard_pressed[i] =
                    input.keyboard.pressed[i] ? uint8_t{ 1 } : uint8_t{ 0 };
                out.keyboard_released[i] =
                    input.keyboard.released[i] ? uint8_t{ 1 } : uint8_t{ 0 };
            }

            out.mouse_x = input.mouse.x;
            out.mouse_y = input.mouse.y;
            out.mouse_dx = input.mouse.dx;
            out.mouse_dy = input.mouse.dy;
            for (uint32_t i = 0; i < 3; ++i) {
                out.mouse_down[i] =
                    input.mouse.down[i] ? uint8_t{ 1 } : uint8_t{ 0 };
                out.mouse_pressed[i] =
                    input.mouse.pressed[i] ? uint8_t{ 1 } : uint8_t{ 0 };
                out.mouse_released[i] =
                    input.mouse.released[i] ? uint8_t{ 1 } : uint8_t{ 0 };
            }

            out.window_focused =
                input.window.focused ? uint8_t{ 1 } : uint8_t{ 0 };
            out.window_width = input.window.width;
            out.window_height = input.window.height;

            out.controller_connected =
                input.controller.connected ? uint8_t{ 1 } : uint8_t{ 0 };
            for (uint32_t i = 0; i < 8; ++i) {
                out.controller_axes[i] = input.controller.axes[i];
            }
            for (uint32_t i = 0; i < 16; ++i) {
                out.controller_buttons[i] =
                    input.controller.buttons[i] ? uint8_t{ 1 } : uint8_t{ 0 };
            }
        }

        uint8_t read_collision_event(
            void* user,
            uint32_t index,
            WzCollisionEntityEvent* out_event)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->frame_storage || !out_event) {
                return 0;
            }

            const auto& events =
                context->frame_storage->collision.routed_entity_events;
            if (index >= events.size()) {
                return 0;
            }

            const auto& event = events[index];
            *out_event = WzCollisionEntityEvent{
                .entity = event.entity,
                .other = event.other,
                .kind = to_abi_collision_kind(event.kind),
                .self_is_trigger =
                    event.self_is_trigger ? uint8_t{ 1 } : uint8_t{ 0 },
            };
            return 1;
        }

        uint8_t write_behavior_command(
            void* user,
            const WzBehaviorCommand* command)
        {
            if (!user || !command) {
                return 0;
            }

            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context->commands) {
                return 0;
            }

            const BehaviorCommandKind kind =
                from_abi_command_kind(command->kind);
            if (kind == BehaviorCommandKind::None) {
                return 0;
            }

            context->commands->commands.push_back(BehaviorCommand{
                .entity = command->entity,
                .kind = kind,
                .values = {
                    command->values[0],
                    command->values[1],
                    command->values[2],
                    command->values[3],
                },
            });
            return 1;
        }

        void log_info(void* user, const char* message)
        {
            auto* logger = static_cast<wz::Logger*>(user);
            if (!logger || !message) {
                return;
            }
            logger->info(message);
        }

        void dispatch_abi_behavior(
            BehaviorFrameContext& context,
            wz::scene::RuntimeEntityId entity,
            void* user_data)
        {
            auto* binding =
                static_cast<BehaviorPluginHost::Binding*>(user_data);
            if (!binding || !binding->function) {
                return;
            }

            WzInputStateView input_view{};
            const WzInputStateView* input = nullptr;
            if (context.frame_context) {
                fill_input_view(context.frame_context->input, input_view);
                input = &input_view;
            }

            uint32_t collision_event_count = 0;
            if (context.frame_storage) {
                const auto& routed =
                    context.frame_storage->collision.routed_entity_events;
                collision_event_count =
                    static_cast<uint32_t>(
                        std::min<std::size_t>(
                            routed.size(),
                            UINT32_MAX));
            }

            WzBehaviorFrameFacts facts{
                .input = input,
                .collision_events = WzCollisionEntityEventView{
                    .user = &context,
                    .count = collision_event_count,
                    .read = read_collision_event,
                },
                .command_writer_user = &context,
                .write_command = write_behavior_command,
                .log_user = binding->logger,
                .log_info = log_info,
            };

            binding->function(&facts, entity, binding->user_data);
        }

        WzBehaviorFrameFacts make_frame_facts(
            BehaviorFrameContext& context,
            wz::Logger* logger,
            WzInputStateView& input_view)
        {
            const WzInputStateView* input = nullptr;
            if (context.frame_context) {
                fill_input_view(context.frame_context->input, input_view);
                input = &input_view;
            }

            uint32_t collision_event_count = 0;
            if (context.frame_storage) {
                const auto& routed =
                    context.frame_storage->collision.routed_entity_events;
                collision_event_count =
                    static_cast<uint32_t>(
                        std::min<std::size_t>(
                            routed.size(),
                            UINT32_MAX));
            }

            return WzBehaviorFrameFacts{
                .input = input,
                .collision_events = WzCollisionEntityEventView{
                    .user = &context,
                    .count = collision_event_count,
                    .read = read_collision_event,
                },
                .command_writer_user = &context,
                .write_command = write_behavior_command,
                .log_user = logger,
                .log_info = log_info,
            };
        }

        void dispatch_abi_module_event(
            BehaviorFrameContext& context,
            const BehaviorEvent& event,
            void* user_data)
        {
            auto* binding =
                static_cast<BehaviorPluginHost::Binding*>(user_data);
            if (!binding || !binding->on_event) {
                return;
            }

            WzInputStateView input_view{};
            WzBehaviorFrameFacts facts =
                make_frame_facts(context, binding->logger, input_view);
            const WzBehaviorEvent abi_event{
                .kind = to_abi_behavior_event_kind(event),
                .entity = event.entity,
                .other = event.other,
                .self_is_trigger =
                    event.self_is_trigger ? uint8_t{ 1 } : uint8_t{ 0 },
            };

            binding->on_event(&facts, &abi_event, binding->user_data);
        }

        uint8_t register_behavior(
            void* user,
            const char* module,
            const char* name,
            WzBehaviorFn function,
            void* behavior_user_data)
        {
            auto* context = static_cast<RegisterContext*>(user);
            if (!context || !context->registry || !context->host
                || !name || !function)
            {
                return 0;
            }

            auto* binding_ptr = context->host->add_binding(
                function,
                behavior_user_data,
                context->logger);
            const BehaviorHandle handle =
                context->registry->register_behavior(
                    module ? module : "",
                    name,
                    dispatch_abi_behavior,
                    binding_ptr);
            return handle.valid() ? uint8_t{ 1 } : uint8_t{ 0 };
        }

        uint8_t register_module(
            void* user,
            const char* module,
            WzBehaviorModuleEventFn on_event,
            void* module_user_data)
        {
            auto* context = static_cast<RegisterContext*>(user);
            if (!context || !context->registry || !context->host
                || !module || !on_event)
            {
                return 0;
            }

            auto* binding_ptr = context->host->add_module_binding(
                on_event,
                module_user_data,
                context->logger);
            const BehaviorModuleHandle handle =
                context->registry->register_module(
                    module,
                    dispatch_abi_module_event,
                    binding_ptr);
            return handle.valid() ? uint8_t{ 1 } : uint8_t{ 0 };
        }
    }

    BehaviorPluginHost::~BehaviorPluginHost()
    {
        clear();
    }

    bool BehaviorPluginHost::register_static_pack(
        BehaviorRegistry& registry,
        WzRegisterBehaviorPluginFn register_plugin,
        wz::Logger* logger,
        uint32_t api_version)
    {
        if (!register_plugin || api_version != WZ_BEHAVIOR_ABI_VERSION) {
            return false;
        }

        RegisterContext context{
            .registry = &registry,
            .host = this,
            .logger = logger,
        };
        WzBehaviorPluginApi api{
            .version = api_version,
            .user = &context,
            .register_behavior = register_behavior,
            .register_module = register_module,
        };

        return register_plugin(&api) != 0;
    }

    BehaviorPluginHost::DynamicLoadResult
    BehaviorPluginHost::load_dynamic_module(
        BehaviorRegistry& registry,
        const std::filesystem::path& path,
        wz::Logger* logger,
        const char* register_symbol)
    {
        if (path.empty() || !std::filesystem::exists(path)) {
            return {
                .status = DynamicLoadStatus::InvalidPath,
                .detail = path.string(),
            };
        }

#if defined(_WIN32)
        HMODULE module = LoadLibraryW(path.wstring().c_str());
        if (!module) {
            const DWORD error = GetLastError();
            return {
                .status = DynamicLoadStatus::LoadFailed,
                .detail =
                    path.string() + " error=" + std::to_string(error),
            };
        }

        const char* symbol =
            register_symbol && register_symbol[0] != '\0'
                ? register_symbol
                : WZ_BEHAVIOR_PLUGIN_REGISTER_SYMBOL;
        auto* register_plugin =
            reinterpret_cast<WzRegisterBehaviorPluginFn>(
                GetProcAddress(module, symbol));
        if (!register_plugin) {
            FreeLibrary(module);
            return {
                .status = DynamicLoadStatus::MissingRegisterSymbol,
                .detail = symbol,
            };
        }

        if (!register_static_pack(registry, register_plugin, logger)) {
            FreeLibrary(module);
            return {
                .status = DynamicLoadStatus::RegistrationFailed,
                .detail = path.string(),
            };
        }

        dynamic_modules_.push_back(DynamicModule{
            .handle = module,
            .path = path.string(),
        });
        return {
            .status = DynamicLoadStatus::Loaded,
            .detail = path.string(),
        };
#else
        (void)registry;
        (void)logger;
        (void)register_symbol;
        return {
            .status = DynamicLoadStatus::UnsupportedPlatform,
            .detail = path.string(),
        };
#endif
    }

    uint32_t BehaviorPluginHost::load_dynamic_modules_from_directory(
        BehaviorRegistry& registry,
        const std::filesystem::path& directory,
        wz::Logger* logger)
    {
        std::error_code ec;
        if (directory.empty()
            || !std::filesystem::exists(directory, ec)
            || ec
            || !std::filesystem::is_directory(directory, ec)
            || ec)
        {
            return 0;
        }

        uint32_t loaded = 0;
        std::filesystem::directory_iterator it{
            directory,
            std::filesystem::directory_options::skip_permission_denied,
            ec,
        };
        if (ec) {
            if (logger) {
                logger->warn(
                    "[behavior] failed to scan behavior module directory: "
                    + directory.string());
            }
            return 0;
        }

        const std::filesystem::directory_iterator end{};
        for (; it != end; it.increment(ec)) {
            if (ec) {
                if (logger) {
                    logger->warn(
                        "[behavior] failed while scanning behavior module "
                        "directory: " + directory.string());
                }
                break;
            }

            const auto& entry = *it;
            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }

            const std::filesystem::path path = entry.path();
            if (ec) {
                ec.clear();
                continue;
            }

#if defined(_WIN32)
            if (path.extension() != ".dll") {
                continue;
            }
#else
            continue;
#endif

            const DynamicLoadResult result =
                load_dynamic_module(registry, path, logger);
            if (result.ok()) {
                ++loaded;
            }
            else if (logger) {
                logger->warn(
                    "[behavior] failed to load behavior module '"
                    + path.string() + "' status="
                    + dynamic_load_status_name(result.status)
                    + " detail=" + result.detail);
            }
        }
        return loaded;
    }

    void BehaviorPluginHost::clear()
    {
        bindings_.clear();
#if defined(_WIN32)
        for (const DynamicModule& module : dynamic_modules_) {
            if (module.handle) {
                FreeLibrary(static_cast<HMODULE>(module.handle));
            }
        }
#endif
        dynamic_modules_.clear();
    }

    BehaviorPluginHost::Binding* BehaviorPluginHost::add_binding(
        WzBehaviorFn function,
        void* user_data,
        wz::Logger* logger)
    {
        auto binding = std::make_unique<Binding>(Binding{
            .function = function,
            .user_data = user_data,
            .logger = logger,
        });
        Binding* out = binding.get();
        bindings_.push_back(std::move(binding));
        return out;
    }

    BehaviorPluginHost::Binding* BehaviorPluginHost::add_module_binding(
        WzBehaviorModuleEventFn on_event,
        void* user_data,
        wz::Logger* logger)
    {
        auto binding = std::make_unique<Binding>(Binding{
            .on_event = on_event,
            .user_data = user_data,
            .logger = logger,
        });
        Binding* out = binding.get();
        bindings_.push_back(std::move(binding));
        return out;
    }
}
