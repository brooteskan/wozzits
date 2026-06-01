#include <engine/behavior/behavior_dispatch.h>
#include <engine/behavior/behavior_command_apply.h>
#include <engine/behavior/builtin_behaviors.h>
#include <engine/behavior/behavior_module_api.h>
#include <engine/behavior/behavior_registry.h>
#include <engine/behavior/sample_collision_behaviors.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scene/scene_json_export.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/frame_storage.h>

#include <external/json/json_writer.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>
#include <scene/scene_graph.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace wz::engine::behavior;
    using wz::engine::assets::BehaviorComponent;
    using wz::engine::assets::SceneComponentRecord;
    using wz::engine::assets::SceneInstance;
    using wz::scene::RuntimeEntityId;

    struct CallCounter
    {
        uint32_t calls = 0;
        RuntimeEntityId last_entity = wz::scene::INVALID_RUNTIME_ENTITY;
    };

    void count_behavior(
        BehaviorFrameContext& context,
        RuntimeEntityId entity,
        void* user_data)
    {
        auto* counter = static_cast<CallCounter*>(user_data);
        ASSERT_NE(counter, nullptr);
        ASSERT_NE(context.commands, nullptr);

        ++counter->calls;
        counter->last_entity = entity;
        context.commands->add_local_translation(entity, 1.0f, 2.0f, 3.0f);
    }

    void bounce_on_collision_enter(
        BehaviorFrameContext& context,
        RuntimeEntityId entity,
        void*)
    {
        ASSERT_NE(context.frame_storage, nullptr);
        ASSERT_NE(context.commands, nullptr);

        for (const auto& event :
            context.frame_storage->collision.routed_entity_events)
        {
            if (event.entity == entity
                && event.kind
                    == wz::engine::collision::CollisionEventKind::Enter)
            {
                context.commands->add_local_translation(
                    entity,
                    0.0f,
                    8.0f,
                    0.0f);
            }
        }
    }

    SceneInstance scene_with_behavior(
        RuntimeEntityId entity,
        std::string module,
        std::string name,
        bool enabled = true)
    {
        SceneInstance scene{};
        scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
            .node = entity,
            .component = BehaviorComponent{
                .module = std::move(module),
                .name = std::move(name),
                .enabled = enabled,
            },
        });
        return scene;
    }

    wz::fs::Path write_text(
        const wz::fs::Path& root,
        const std::string& filename,
        const std::string& content)
    {
        const wz::fs::Path path = wz::fs::join(root, filename);
        wz::fs::write_file_text(path, content);
        return filename;
    }
}

TEST(BehaviorRegistry, RegistersAndFindsStaticBehavior)
{
    BehaviorRegistry registry;
    CallCounter counter{};

    const BehaviorHandle handle = registry.register_behavior(
        "gameplay",
        "count",
        count_behavior,
        &counter);

    ASSERT_TRUE(handle.valid());
    const auto found = registry.find("gameplay", "count");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->index, handle.index);

    const BehaviorRegistration* registration = registry.get(handle);
    ASSERT_NE(registration, nullptr);
    EXPECT_EQ(registration->module, "gameplay");
    EXPECT_EQ(registration->name, "count");
    EXPECT_EQ(registration->function, count_behavior);
    EXPECT_EQ(registration->user_data, &counter);
}

TEST(BehaviorRegistry, ReRegisteringBehaviorUpdatesFunctionSlot)
{
    BehaviorRegistry registry;
    CallCounter first{};
    CallCounter second{};

    const BehaviorHandle a = registry.register_behavior(
        "gameplay",
        "count",
        count_behavior,
        &first);
    const BehaviorHandle b = registry.register_behavior(
        "gameplay",
        "count",
        count_behavior,
        &second);

    EXPECT_EQ(a.index, b.index);
    ASSERT_EQ(registry.registrations().size(), 1u);
    EXPECT_EQ(registry.get(a)->user_data, &second);
}

TEST(BehaviorRegistry, RegistersBuiltinDebugBehaviorPack)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    wz::Logger logger{};

    register_builtin_behaviors(registry, plugins, logger);

    const auto found_log = registry.find(
        kDebugBehaviorModule,
        kLogCollisionEventsBehavior);
    ASSERT_TRUE(found_log.has_value());

    const BehaviorRegistration* registration = registry.get(*found_log);
    ASSERT_NE(registration, nullptr);
    EXPECT_EQ(registration->module, "debug");
    EXPECT_EQ(registration->name, "log_collision_events");
    EXPECT_NE(registration->function, nullptr);

    const auto found_bounce = registry.find(
        kSampleBehaviorModule,
        kBounceOnCollisionEnterBehavior);
    ASSERT_TRUE(found_bounce.has_value());
}

namespace
{
    uint8_t register_empty_pack(WzBehaviorPluginApi* api)
    {
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION ? 1 : 0;
    }

    struct AbiBoundaryProbe
    {
        uint32_t calls = 0;
        uint32_t observed_count = UINT32_MAX;
        bool read_was_present = false;
        uint8_t out_of_range_read = 1;
        uint8_t null_out_read = 1;
        bool out_event_preserved = false;
        uint8_t none_write = 1;
        uint8_t bad_write = 1;
    };

    AbiBoundaryProbe* g_boundary_probe = nullptr;

    void boundary_probe_behavior(
        const WzBehaviorFrameFacts* facts,
        WzBehaviorEntityId,
        void* user)
    {
        auto* probe = static_cast<AbiBoundaryProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);

        ++probe->calls;
        probe->observed_count = facts->collision_events.count;
        probe->read_was_present = facts->collision_events.read != nullptr;

        WzCollisionEntityEvent out{
            .entity = 123u,
            .other = 456u,
            .kind = 789u,
            .self_is_trigger = 1u,
        };
        probe->out_of_range_read = facts->collision_events.read(
            facts->collision_events.user,
            facts->collision_events.count,
            &out);
        probe->out_event_preserved =
            out.entity == 123u
            && out.other == 456u
            && out.kind == 789u
            && out.self_is_trigger == 1u;
        probe->null_out_read = facts->collision_events.read(
            facts->collision_events.user,
            0u,
            nullptr);

        const WzBehaviorCommand none_command{
            .entity = 7u,
            .kind = WZ_BEHAVIOR_COMMAND_NONE,
            .values = { 1.0f, 2.0f, 3.0f, 0.0f },
        };
        probe->none_write = facts->write_command(
            facts->command_writer_user,
            &none_command);

        const WzBehaviorCommand bad_command{
            .entity = 7u,
            .kind = 999u,
            .values = { 1.0f, 2.0f, 3.0f, 0.0f },
        };
        probe->bad_write = facts->write_command(
            facts->command_writer_user,
            &bad_command);
    }

    uint8_t register_boundary_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_behavior) {
            return 0;
        }
        return api->register_behavior(
            api->user,
            "test",
            "boundary_probe",
            boundary_probe_behavior,
            g_boundary_probe);
    }

    struct InvalidRegistrationProbe
    {
        uint8_t null_name_result = 1;
        uint8_t null_function_result = 1;
    };

    InvalidRegistrationProbe* g_invalid_registration_probe = nullptr;

    void no_op_abi_behavior(
        const WzBehaviorFrameFacts*,
        WzBehaviorEntityId,
        void*)
    {
    }

    struct ModuleEventProbe
    {
        uint32_t calls = 0;
        std::vector<WzBehaviorEventKind> kinds;
        std::vector<WzBehaviorEntityId> entities;
        std::vector<WzBehaviorEntityId> others;
        WzBehaviorEventKind last_kind = WZ_EVENT_NONE;
        WzBehaviorEntityId last_entity = WZ_INVALID_BEHAVIOR_ENTITY;
        WzBehaviorEntityId last_other = WZ_INVALID_BEHAVIOR_ENTITY;
        uint8_t last_trigger = 0;
        bool wrote_command = false;
    };

    ModuleEventProbe* g_module_event_probe = nullptr;

    void module_event_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<ModuleEventProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);

        ++probe->calls;
        probe->kinds.push_back(event->kind);
        probe->entities.push_back(event->entity);
        probe->others.push_back(event->other);
        probe->last_kind = event->kind;
        probe->last_entity = event->entity;
        probe->last_other = event->other;
        probe->last_trigger = event->self_is_trigger;

        if (event->kind == WZ_EVENT_COLLISION_ENTER
            && facts->write_command)
        {
            const WzBehaviorCommand command{
                .entity = event->entity,
                .kind = WZ_BEHAVIOR_COMMAND_ADD_LOCAL_TRANSLATION,
                .values = { 0.0f, 3.0f, 0.0f, 0.0f },
            };
            probe->wrote_command =
                facts->write_command(facts->command_writer_user, &command)
                != 0;
        }
    }

    uint8_t register_module_event_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module || !g_module_event_probe) {
            return 0;
        }

        return api->register_module(
            api->user,
            "module_test",
            module_event_handler,
            g_module_event_probe);
    }

    struct ModuleHelperProbe
    {
        uint32_t calls = 0;
        uint8_t wrote_command = 0;
    };

    ModuleHelperProbe* g_module_helper_probe = nullptr;

    void module_helper_event_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<ModuleHelperProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);

        ++probe->calls;
        if (wz_is_event(event, WZ_EVENT_COLLISION_ENTER)) {
            probe->wrote_command = wz_self_add_local_translation(
                facts,
                event,
                2.0f,
                4.0f,
                6.0f);
        }
    }

    uint8_t register_module_helper_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module || !g_module_helper_probe) {
            return 0;
        }

        return api->register_module(
            api->user,
            "module_helper_test",
            module_helper_event_handler,
            g_module_helper_probe);
    }

    struct TransformCommandProbe
    {
        uint32_t calls = 0;
        uint8_t wrote_set_scale = 0;
        uint8_t wrote_add_scale = 0;
        uint8_t wrote_set_rotation = 0;
    };

    TransformCommandProbe* g_transform_command_probe = nullptr;

    void transform_command_event_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<TransformCommandProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);

        ++probe->calls;
        if (!wz_is_event(event, WZ_EVENT_COLLISION_ENTER)) {
            return;
        }

        probe->wrote_set_scale = wz_self_set_local_scale(
            facts,
            event,
            2.0f,
            3.0f,
            4.0f);
        probe->wrote_add_scale = wz_self_add_local_scale(
            facts,
            event,
            1.0f,
            1.0f,
            1.0f);
        probe->wrote_set_rotation = wz_self_set_local_rotation(
            facts,
            event,
            WzQuaternion{
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.70710677f,
                .w = 0.70710677f,
            });
    }

    uint8_t register_transform_command_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module || !g_transform_command_probe) {
            return 0;
        }

        return api->register_module(
            api->user,
            "transform_command_test",
            transform_command_event_handler,
            g_transform_command_probe);
    }

    struct TransformQueryProbe
    {
        uint32_t calls = 0;
        uint8_t frame_update_other_position_result = 1;
        uint8_t self_local_position_result = 0;
        uint8_t self_world_position_result = 0;
        uint8_t other_world_position_result = 0;
        uint8_t self_local_transform_result = 0;
        uint8_t invalid_entity_position_result = 1;
        uint8_t null_out_transform_result = 1;
        WzVec3 self_local_position{};
        WzVec3 self_world_position{};
        WzVec3 other_world_position{};
        WzMat4 self_local_transform{};
    };

    TransformQueryProbe* g_transform_query_probe = nullptr;

    void transform_query_event_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<TransformQueryProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);

        ++probe->calls;
        if (wz_is_event(event, WZ_EVENT_FRAME_UPDATE)) {
            WzVec3 unused{};
            probe->frame_update_other_position_result =
                wz_other_world_position(facts, event, &unused);
            return;
        }

        if (!wz_is_event(event, WZ_EVENT_COLLISION_ENTER)) {
            return;
        }

        probe->self_local_position_result =
            wz_self_local_position(
                facts,
                event,
                &probe->self_local_position);
        probe->self_world_position_result =
            wz_self_world_position(
                facts,
                event,
                &probe->self_world_position);
        probe->other_world_position_result =
            wz_other_world_position(
                facts,
                event,
                &probe->other_world_position);
        probe->self_local_transform_result =
            wz_self_local_transform(
                facts,
                event,
                &probe->self_local_transform);
        probe->invalid_entity_position_result =
            wz_read_world_position(
                facts,
                WZ_INVALID_BEHAVIOR_ENTITY,
                &probe->other_world_position);
        probe->null_out_transform_result =
            wz_self_world_transform(facts, event, nullptr);
    }

    uint8_t register_transform_query_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module || !g_transform_query_probe) {
            return 0;
        }

        return api->register_module(
            api->user,
            "transform_query_test",
            transform_query_event_handler,
            g_transform_query_probe);
    }

    uint8_t register_invalid_registration_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_behavior
            || !g_invalid_registration_probe)
        {
            return 0;
        }

        g_invalid_registration_probe->null_name_result =
            api->register_behavior(
                api->user,
                "test",
                nullptr,
                no_op_abi_behavior,
                nullptr);
        g_invalid_registration_probe->null_function_result =
            api->register_behavior(
                api->user,
                "test",
                "null_function",
                nullptr,
                nullptr);
        return 1;
    }

    struct LogCapture
    {
        std::vector<std::string> messages;
    };

    void capture_log(
        const wz::logging::LogRecordView& record,
        void* user)
    {
        auto* capture = static_cast<LogCapture*>(user);
        if (!capture || !record.text) {
            return;
        }
        capture->messages.emplace_back(record.text, record.text_size);
    }
}

TEST(BehaviorPluginAbi, RejectsVersionMismatch)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    EXPECT_FALSE(plugins.register_static_pack(
        registry,
        register_empty_pack,
        nullptr,
        WZ_BEHAVIOR_ABI_VERSION + 1u));
    EXPECT_TRUE(registry.registrations().empty());
}

TEST(BehaviorPluginDynamicModule, RejectsMissingPath)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    const auto result = plugins.load_dynamic_module(
        registry,
        std::filesystem::path{
            "definitely_missing_behavior_plugin_wozzits_test.dll" });

    EXPECT_EQ(
        result.status,
        BehaviorPluginHost::DynamicLoadStatus::InvalidPath);
    EXPECT_TRUE(registry.registrations().empty());
}

TEST(BehaviorPluginDynamicModule, RejectsMissingRegisterSymbol)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    const auto result = plugins.load_dynamic_module(
        registry,
        std::filesystem::path{ WZ_TEST_BEHAVIOR_MISSING_SYMBOL_DLL });

    EXPECT_EQ(
        result.status,
        BehaviorPluginHost::DynamicLoadStatus::MissingRegisterSymbol);
    EXPECT_TRUE(registry.registrations().empty());
}

TEST(BehaviorPluginDynamicModule, LoadsRegistersAndDispatchesBehavior)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    const auto result = plugins.load_dynamic_module(
        registry,
        std::filesystem::path{ WZ_TEST_BEHAVIOR_PLUGIN_DLL });

    ASSERT_TRUE(result.ok()) << result.detail;
    ASSERT_TRUE(registry.find(
        "dynamic_test",
        "always_add_local_y").has_value());

    SceneInstance scene = scene_with_behavior(
        12u,
        "dynamic_test",
        "always_add_local_y");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 12u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 0.0f);
    EXPECT_FLOAT_EQ(command.values[1], 2.0f);
    EXPECT_FLOAT_EQ(command.values[2], 0.0f);
}

TEST(BehaviorPluginAbi, RejectsInvalidRegistrations)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    InvalidRegistrationProbe probe{};
    g_invalid_registration_probe = &probe;

    EXPECT_TRUE(plugins.register_static_pack(
        registry,
        register_invalid_registration_pack));
    g_invalid_registration_probe = nullptr;

    EXPECT_EQ(probe.null_name_result, 0u);
    EXPECT_EQ(probe.null_function_result, 0u);
    EXPECT_TRUE(registry.registrations().empty());
}

TEST(BehaviorDispatch, DispatchesRoutedCollisionEventsToRegisteredModule)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "ignored_function_name");

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
            .self_is_trigger = true,
        },
    };

    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    ASSERT_EQ(probe.kinds.size(), 2u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.last_kind, WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.last_entity, 4u);
    EXPECT_EQ(probe.last_other, 9u);
    EXPECT_EQ(probe.last_trigger, 1u);
    EXPECT_TRUE(probe.wrote_command);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::AddLocalTranslation);

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, DispatchesFrameUpdateToRegisteredModule)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "");
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.last_kind, WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.last_entity, 4u);
    EXPECT_EQ(probe.last_other, WZ_INVALID_BEHAVIOR_ENTITY);
    EXPECT_EQ(probe.last_trigger, 0u);
    EXPECT_FALSE(probe.wrote_command);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, FrameUpdateRepeatsAndCollisionEventsAreFrameLocal)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "");
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 2u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_COLLISION_ENTER);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);

    frame_storage.collision.routed_entity_events.clear();
    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 3u);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_FRAME_UPDATE);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty())
        << "dispatch clears prior-frame collision commands";

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, ModuleAndLegacyBehaviorComposeOnSameEntity)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    CallCounter counter{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));
    registry.register_behavior("module_test", "count", count_behavior, &counter);

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "count");
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 2u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(counter.calls, 1u);
    EXPECT_EQ(counter.last_entity, 4u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 2u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[0].values[1],
        3.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[1].entity, 4u);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[1].values[0],
        1.0f);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[1].values[1],
        2.0f);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[1].values[2],
        3.0f);

    g_module_event_probe = nullptr;
}

TEST(BehaviorModuleApi, NullEventHelpersReturnSentinels)
{
    EXPECT_EQ(wz_self(nullptr), WZ_INVALID_BEHAVIOR_ENTITY);
    EXPECT_EQ(wz_other(nullptr), WZ_INVALID_BEHAVIOR_ENTITY);
    EXPECT_EQ(wz_event_kind(nullptr), WZ_EVENT_NONE);
    EXPECT_EQ(wz_is_event(nullptr, WZ_EVENT_FRAME_UPDATE), 0u);
    EXPECT_EQ(wz_self_is_trigger(nullptr), 0u);
}

TEST(BehaviorModuleApi, SelfAddLocalTranslationWritesCommandForEventEntity)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleHelperProbe probe{};
    g_module_helper_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_helper_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_helper_test",
        "");
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.wrote_command, 1u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 4u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 2.0f);
    EXPECT_FLOAT_EQ(command.values[1], 4.0f);
    EXPECT_FLOAT_EQ(command.values[2], 6.0f);

    g_module_helper_probe = nullptr;
}

TEST(BehaviorModuleApi, TransformQueriesReadSelfAndOtherSceneTransforms)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_transform_query_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.translation[0] = 10.0f;
    root.local.translation[2] = 1.0f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.parent_id = "root";
    actor.local.translation[1] = 2.0f;
    actor.local.translation[2] = 3.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "transform_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId root_id = scene.authored_to_runtime["root"];
    const RuntimeEntityId actor_id = scene.authored_to_runtime["actor"];

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    TransformQueryProbe probe{};
    g_transform_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_transform_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = root_id,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.frame_update_other_position_result, 0u);
    EXPECT_EQ(probe.self_local_position_result, 1u);
    EXPECT_EQ(probe.self_world_position_result, 1u);
    EXPECT_EQ(probe.other_world_position_result, 1u);
    EXPECT_EQ(probe.self_local_transform_result, 1u);
    EXPECT_EQ(probe.invalid_entity_position_result, 0u);
    EXPECT_EQ(probe.null_out_transform_result, 0u);

    EXPECT_FLOAT_EQ(probe.self_local_position.x, 0.0f);
    EXPECT_FLOAT_EQ(probe.self_local_position.y, 2.0f);
    EXPECT_FLOAT_EQ(probe.self_local_position.z, 3.0f);
    EXPECT_FLOAT_EQ(probe.self_world_position.x, 10.0f);
    EXPECT_FLOAT_EQ(probe.self_world_position.y, 2.0f);
    EXPECT_FLOAT_EQ(probe.self_world_position.z, 4.0f);
    EXPECT_FLOAT_EQ(probe.other_world_position.x, 10.0f);
    EXPECT_FLOAT_EQ(probe.other_world_position.y, 0.0f);
    EXPECT_FLOAT_EQ(probe.other_world_position.z, 1.0f);
    EXPECT_FLOAT_EQ(probe.self_local_transform.m[12], 0.0f);
    EXPECT_FLOAT_EQ(probe.self_local_transform.m[13], 2.0f);
    EXPECT_FLOAT_EQ(probe.self_local_transform.m[14], 3.0f);

    g_transform_query_probe = nullptr;
}

TEST(BehaviorModuleApi, SelfTransformCommandHelpersWriteCommandsForEventEntity)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    TransformCommandProbe probe{};
    g_transform_command_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_transform_command_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "transform_command_test",
        "");
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.wrote_set_scale, 1u);
    EXPECT_EQ(probe.wrote_add_scale, 1u);
    EXPECT_EQ(probe.wrote_set_rotation, 1u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 3u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::SetLocalScale);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[0].values[0], 2.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[0].values[1], 3.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[0].values[2], 4.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[1].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[1].kind,
        BehaviorCommandKind::AddLocalScale);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[1].values[0], 1.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[1].values[1], 1.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[1].values[2], 1.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[2].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[2].kind,
        BehaviorCommandKind::SetLocalRotation);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[2].values[0], 0.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[2].values[1], 0.0f);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[2].values[2],
        0.70710677f);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[2].values[3],
        0.70710677f);

    g_transform_command_probe = nullptr;
}

TEST(BehaviorDispatch, RunsEnabledSceneBehaviorAndWritesCommands)
{
    BehaviorRegistry registry;
    CallCounter counter{};
    registry.register_behavior("count", count_behavior, &counter);

    SceneInstance scene = scene_with_behavior(7u, "", "count");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(counter.calls, 1u);
    EXPECT_EQ(counter.last_entity, 7u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 7u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 1.0f);
    EXPECT_FLOAT_EQ(command.values[1], 2.0f);
    EXPECT_FLOAT_EQ(command.values[2], 3.0f);
}

TEST(BehaviorDispatch, SkipsDisabledOrMissingBehaviors)
{
    BehaviorRegistry registry;
    CallCounter counter{};
    registry.register_behavior("count", count_behavior, &counter);

    SceneInstance scene{};
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 1u,
        .component = BehaviorComponent{
            .name = "count",
            .enabled = false,
        },
    });
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 2u,
        .component = BehaviorComponent{
            .name = "missing",
            .enabled = true,
        },
    });

    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.behavior_commands.add_local_translation(
        99u,
        1.0f,
        1.0f,
        1.0f);
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(counter.calls, 0u);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty())
        << "dispatch clears stale behavior commands each frame";
}

TEST(BehaviorDispatch, BehaviorConsumesRoutedCollisionFacts)
{
    BehaviorRegistry registry;
    registry.register_behavior(
        "gameplay",
        "bounce_on_collision",
        bounce_on_collision_enter);

    SceneInstance scene =
        scene_with_behavior(4u, "gameplay", "bounce_on_collision");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 5u,
            .other = 4u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 4u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 0.0f);
    EXPECT_FLOAT_EQ(command.values[1], 8.0f);
    EXPECT_FLOAT_EQ(command.values[2], 0.0f);
}

TEST(BehaviorDispatch, AbiSampleBounceConsumesRoutedCollisionFacts)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    wz::Logger logger{};
    register_builtin_behaviors(registry, plugins, logger);

    SceneInstance scene = scene_with_behavior(
        4u,
        kSampleBehaviorModule,
        kBounceOnCollisionEnterBehavior);
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Stay,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 5u,
            .other = 4u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 4u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 0.0f);
    EXPECT_FLOAT_EQ(command.values[1], 1.0f);
    EXPECT_FLOAT_EQ(command.values[2], 0.0f);
}

TEST(BehaviorPluginAbi, CollisionViewAndCommandWriterRejectBoundaries)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    AbiBoundaryProbe probe{};
    g_boundary_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_boundary_pack));
    g_boundary_probe = nullptr;

    SceneInstance scene =
        scene_with_behavior(7u, "test", "boundary_probe");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.observed_count, 0u);
    EXPECT_TRUE(probe.read_was_present);
    EXPECT_EQ(probe.out_of_range_read, 0u);
    EXPECT_EQ(probe.null_out_read, 0u);
    EXPECT_TRUE(probe.out_event_preserved);
    EXPECT_EQ(probe.none_write, 0u);
    EXPECT_EQ(probe.bad_write, 0u);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());
}

TEST(BehaviorPluginAbi, MultipleAbiBehaviorsReadSameFrameView)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    wz::Logger logger{};
    register_builtin_behaviors(registry, plugins, logger);

    SceneInstance scene{};
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 4u,
        .component = BehaviorComponent{
            .module = kSampleBehaviorModule,
            .name = kBounceOnCollisionEnterBehavior,
            .enabled = true,
        },
    });
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 5u,
        .component = BehaviorComponent{
            .module = kSampleBehaviorModule,
            .name = kBounceOnCollisionEnterBehavior,
            .enabled = true,
        },
    });

    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 5u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 2u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[1].entity, 5u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::AddLocalTranslation);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[1].kind,
        BehaviorCommandKind::AddLocalTranslation);
}

TEST(BehaviorPluginAbi, DebugLogBehaviorWritesThroughLogCallback)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    LogCapture capture{};
    wz::Logger logger{};
    ASSERT_TRUE(wz::logging::init_logger(
        logger,
        wz::logging::LoggerDesc{
            .min_level = wz::LogLevel::Debug,
            .enable_stderr_sink = false,
        }));
    wz::logging::set_log_sink(logger, capture_log, &capture);
    register_builtin_behaviors(registry, plugins, logger);

    SceneInstance scene =
        scene_with_behavior(4u, kDebugBehaviorModule, kLogCollisionEventsBehavior);
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
            .self_is_trigger = true,
        },
    };
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);
    wz::logging::wait_until_idle(logger);
    wz::logging::shutdown_logger(logger);

    const auto found = std::find_if(
        capture.messages.begin(),
        capture.messages.end(),
        [](const std::string& message) {
            return message.find("collision.enter") != std::string::npos
                && message.find("entity=4") != std::string::npos
                && message.find("other=9") != std::string::npos;
        });
    EXPECT_NE(found, capture.messages.end());
}

TEST(BehaviorCommands, ApplyLocalTranslationCommandsUpdatesSceneGraph)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.translation[0] = 10.0f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.local.translation[1] = 2.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.add_local_translation(child_id, 1.0f, 2.0f, 3.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], child_id);
    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_FLOAT_EQ(child_node.local.m[12], 1.0f);
    EXPECT_FLOAT_EQ(child_node.local.m[13], 4.0f);
    EXPECT_FLOAT_EQ(child_node.local.m[14], 3.0f);
    EXPECT_FLOAT_EQ(child_node.world.m[12], 11.0f);
    EXPECT_FLOAT_EQ(child_node.world.m[13], 4.0f);
    EXPECT_FLOAT_EQ(child_node.world.m[14], 3.0f);
}

TEST(BehaviorCommands, ApplySetLocalTranslationIgnoresInvalidEntities)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_invalid_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_local_translation(actor, 4.0f, 5.0f, 6.0f);
    commands.add_local_translation(actor, 1.0f, 1.0f, 1.0f);
    commands.set_local_translation(1000u, 1.0f, 1.0f, 1.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 2u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 5.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 6.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 7.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 5.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 6.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 7.0f);
}

TEST(BehaviorCommands, MultipleAddCommandsAccumulateInOrder)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_accumulate_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.translation[0] = 1.0f;
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.add_local_translation(actor, 1.0f, 0.0f, 0.0f);
    commands.add_local_translation(actor, 0.0f, 2.0f, 0.0f);
    commands.add_local_translation(actor, 0.0f, 0.0f, 3.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 3u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 3.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 3.0f);
}

// ─── Adversarial tests ──────────────────────────────────────────────

TEST(Adversarial, DisabledBehaviorReceivesNoModuleEvents)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "",
        false);
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 0u)
        << "disabled behavior component must not receive frame_update or "
           "collision events through the module path";
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());

    g_module_event_probe = nullptr;
}

TEST(Adversarial, EmptyModuleNameSkipsModuleDispatch)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(4u, "", "some_name");
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 0u)
        << "empty module name must skip both frame_update and collision "
           "module dispatch";

    g_module_event_probe = nullptr;
}

TEST(Adversarial, CollisionEventForEntityWithNoBehaviorComponentIsIgnored)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "");
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 99u,
            .other = 4u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 1u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE)
        << "only frame_update for entity 4; collision for entity 99 (no "
           "behavior component) must be ignored";

    g_module_event_probe = nullptr;
}

TEST(Adversarial, DispatchWithNoRegisteredModulesOrBehaviors)
{
    BehaviorRegistry registry;
    SceneInstance scene = scene_with_behavior(
        4u,
        "nonexistent_module",
        "nonexistent_behavior");
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty())
        << "dispatch with empty registry must produce no commands";
}

TEST(Adversarial, DispatchWithNullFrameStorageSkipsCollisionButRunsUpdate)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "");
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = nullptr,
        .frame_storage = nullptr,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 1u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE)
        << "frame_update should fire even with null frame_storage; "
           "collision dispatch should be skipped";

    g_module_event_probe = nullptr;
}

TEST(Adversarial, DispatchWithNullCommandBufferReturnsEarly)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "");
    BehaviorFrameContext context{
        .scene = &scene,
        .commands = nullptr,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 0u)
        << "null command buffer must cause early return before any dispatch";

    g_module_event_probe = nullptr;
}

TEST(Adversarial, EmptySceneDispatchesNothing)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 0u);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());

    g_module_event_probe = nullptr;
}

TEST(Adversarial, MultipleEntitiesSameModuleDifferentCollisionKinds)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene{};
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 4u,
        .component = BehaviorComponent{
            .module = "module_test",
            .enabled = true,
        },
    });
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 5u,
        .component = BehaviorComponent{
            .module = "module_test",
            .enabled = true,
        },
    });

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 5u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 5u,
            .other = 4u,
            .kind = wz::engine::collision::CollisionEventKind::Exit,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 4u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.entities[0], 4u);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.entities[1], 5u);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.entities[2], 4u);
    EXPECT_EQ(probe.others[2], 5u);
    EXPECT_EQ(probe.kinds[3], WZ_EVENT_COLLISION_EXIT);
    EXPECT_EQ(probe.entities[3], 5u);
    EXPECT_EQ(probe.others[3], 4u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u)
        << "only collision.enter writes a command; exit does not";
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);

    g_module_event_probe = nullptr;
}

TEST(Adversarial, ApplyCommandsWithInvalidRuntimeEntityId)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "adversarial_invalid_entity";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    BehaviorCommandBuffer commands{};
    commands.add_local_translation(
        wz::scene::INVALID_RUNTIME_ENTITY, 1.0f, 2.0f, 3.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 0u);
    EXPECT_TRUE(changed.empty());
}

TEST(Adversarial, ApplyEmptyCommandBufferIsNoOp)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "adversarial_empty_commands";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.translation[0] = 5.0f;
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    BehaviorCommandBuffer commands{};
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 0u);
    EXPECT_TRUE(changed.empty());

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 5.0f)
        << "empty command buffer must not touch transforms";
}

TEST(Adversarial, ApplyCommandsWithNoneKindIgnored)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "adversarial_none_kind";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.commands.push_back(BehaviorCommand{
        .entity = actor,
        .kind = BehaviorCommandKind::None,
        .values = { 99.0f, 99.0f, 99.0f, 0.0f },
    });
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 0u);
    EXPECT_TRUE(changed.empty());
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 0.0f)
        << "BehaviorCommandKind::None must not mutate transforms";
}

TEST(Adversarial, ApplyCommandsWithNullChangedEntitiesVector)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "adversarial_null_changed";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.add_local_translation(actor, 1.0f, 2.0f, 3.0f);

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        nullptr);

    EXPECT_EQ(applied, 1u);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 1.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 3.0f);
}

TEST(Adversarial, RegisterModuleWithEmptyNameFails)
{
    BehaviorRegistry registry;

    const BehaviorModuleHandle handle = registry.register_module(
        "",
        [](BehaviorFrameContext&, const BehaviorEvent&, void*) {},
        nullptr);

    EXPECT_FALSE(handle.valid());
    EXPECT_TRUE(registry.modules().empty());
}

TEST(Adversarial, RegisterModuleWithNullFunctionFails)
{
    BehaviorRegistry registry;

    const BehaviorModuleHandle handle = registry.register_module(
        "test_module",
        nullptr,
        nullptr);

    EXPECT_FALSE(handle.valid());
    EXPECT_TRUE(registry.modules().empty());
}

TEST(Adversarial, ReRegisterModuleReplacesHandler)
{
    BehaviorRegistry registry;
    uint32_t call_count_a = 0;
    uint32_t call_count_b = 0;

    auto handler_a = [](BehaviorFrameContext&,
                        const BehaviorEvent&,
                        void* user)
    {
        ++(*static_cast<uint32_t*>(user));
    };
    auto handler_b = [](BehaviorFrameContext&,
                        const BehaviorEvent&,
                        void* user)
    {
        ++(*static_cast<uint32_t*>(user));
    };

    const BehaviorModuleHandle a = registry.register_module(
        "test_module",
        handler_a,
        &call_count_a);
    const BehaviorModuleHandle b = registry.register_module(
        "test_module",
        handler_b,
        &call_count_b);

    EXPECT_EQ(a.index, b.index);
    ASSERT_EQ(registry.modules().size(), 1u);

    const BehaviorModuleRegistration* reg = registry.get_module(a);
    ASSERT_NE(reg, nullptr);
    EXPECT_EQ(reg->on_event, handler_b);
    EXPECT_EQ(reg->user_data, &call_count_b);
}

TEST(Adversarial, FindModuleInvalidHandleReturnsNull)
{
    BehaviorRegistry registry;

    BehaviorModuleHandle bad_handle{ .index = 42u };
    EXPECT_EQ(registry.get_module(bad_handle), nullptr);

    BehaviorModuleHandle invalid_handle{};
    EXPECT_EQ(registry.get_module(invalid_handle), nullptr);

    EXPECT_FALSE(registry.find_module("nonexistent").has_value());
}

TEST(Adversarial, CollisionStayAndExitRouteCorrectEventKinds)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "");
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Stay,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Exit,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 3u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_COLLISION_STAY);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_COLLISION_EXIT);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty())
        << "stay and exit events should not write commands in the test handler";

    g_module_event_probe = nullptr;
}

TEST(BehaviorCommands, ApplyLocalScaleCommandsUpdatesSceneGraph)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_scale_scene";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.scale[0] = 2.0f;
    actor.local.scale[1] = 2.0f;
    actor.local.scale[2] = 2.0f;
    asset.nodes.push_back(std::move(actor));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "actor";
    child.local.translation[0] = 1.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.set_local_scale(actor_id, 3.0f, 4.0f, 5.0f);
    commands.add_local_scale(actor_id, 1.0f, 1.0f, 1.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 2u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor_id);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_FLOAT_EQ(actor_node.local.m[0], 4.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[5], 5.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[10], 6.0f);

    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_FLOAT_EQ(child_node.world.m[12], 4.0f);
}

TEST(BehaviorCommands, ApplySetLocalRotationPreservesTranslationAndScale)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_rotation_scene";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 10.0f;
    actor.local.translation[1] = 20.0f;
    actor.local.translation[2] = 30.0f;
    actor.local.scale[0] = 2.0f;
    actor.local.scale[1] = 3.0f;
    actor.local.scale[2] = 4.0f;
    asset.nodes.push_back(std::move(actor));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "actor";
    child.local.translation[0] = 1.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.set_local_rotation(
        actor_id,
        0.0f,
        0.0f,
        0.70710677f,
        0.70710677f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor_id);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);

    EXPECT_NEAR(actor_node.local.m[0], 0.0f, 1e-5f);
    EXPECT_NEAR(actor_node.local.m[1], 2.0f, 1e-5f);
    EXPECT_NEAR(actor_node.local.m[4], -3.0f, 1e-5f);
    EXPECT_NEAR(actor_node.local.m[5], 0.0f, 1e-5f);
    EXPECT_NEAR(actor_node.local.m[10], 4.0f, 1e-5f);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 10.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 20.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 30.0f);

    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_NEAR(child_node.world.m[12], 10.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[13], 22.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[14], 30.0f, 1e-5f);
}

TEST(BehaviorCommands, SetLocalRotationOnIdentityScaleEntity)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_rotation_identity_scale";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[1] = 5.0f;
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_local_rotation(actor_id, 0.0f, 0.0f, 0.70710677f, 0.70710677f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 1u);
    const auto& node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_NEAR(node.local.m[0], 0.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[1], 1.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[4], -1.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[5], 0.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[10], 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(node.local.m[13], 5.0f)
        << "translation must be preserved";
    float col0_len = std::sqrt(
        node.local.m[0] * node.local.m[0]
        + node.local.m[1] * node.local.m[1]
        + node.local.m[2] * node.local.m[2]);
    EXPECT_NEAR(col0_len, 1.0f, 1e-5f)
        << "unit scale must be preserved";
}

TEST(BehaviorCommands, SequentialSetLocalRotationReplacesNotComposes)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_rotation_replace";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_local_rotation(actor_id, 0.0f, 0.0f, 0.70710677f, 0.70710677f);
    commands.set_local_rotation(actor_id, 0.0f, 0.0f, 0.0f, 1.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 2u);
    const auto& node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_NEAR(node.local.m[0], 1.0f, 1e-5f)
        << "second set_local_rotation(identity) must replace, not compose";
    EXPECT_NEAR(node.local.m[1], 0.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[4], 0.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[5], 1.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[10], 1.0f, 1e-5f);
}

TEST(BehaviorCommands, SetLocalRotationWithInvalidEntityIgnored)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_rotation_invalid";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    BehaviorCommandBuffer commands{};
    commands.set_local_rotation(
        wz::scene::INVALID_RUNTIME_ENTITY,
        0.0f, 0.0f, 0.70710677f, 0.70710677f);
    commands.set_local_rotation(
        1000u,
        0.0f, 0.0f, 0.70710677f, 0.70710677f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 0u);
    EXPECT_TRUE(changed.empty());
}

TEST(BehaviorCommands, SetLocalRotationThenTranslationBothApply)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_rotation_then_translation";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 1.0f;
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_local_rotation(actor_id, 0.0f, 0.0f, 0.70710677f, 0.70710677f);
    commands.set_local_translation(actor_id, 7.0f, 8.0f, 9.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 2u);
    const auto& node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_NEAR(node.local.m[0], 0.0f, 1e-5f);
    EXPECT_NEAR(node.local.m[1], 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(node.local.m[12], 7.0f);
    EXPECT_FLOAT_EQ(node.local.m[13], 8.0f);
    EXPECT_FLOAT_EQ(node.local.m[14], 9.0f);
}

TEST(BehaviorDispatch, SceneBehaviorComponentInstantiates)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "gameplay",
        .name = "bounce_on_collision",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);

    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.behaviors.size(), 1u);
    EXPECT_EQ(result.instance.behaviors[0].node,
        result.instance.authored_to_runtime["actor"]);
    EXPECT_EQ(result.instance.behaviors[0].component.module, "gameplay");
    EXPECT_EQ(
        result.instance.behaviors[0].component.name,
        "bounce_on_collision");
    EXPECT_TRUE(result.instance.behaviors[0].component.enabled);
}

TEST(BehaviorDispatch, SceneBehaviorJsonRoundTrips)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_behavior_scene_json_roundtrip");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "behavior_scene",
  "nodes": [
    {
      "id": "actor",
      "behavior": {
        "module": "gameplay",
        "name": "bounce_on_collision",
        "enabled": true
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path rel_path =
        write_text(root, "behavior.scene.json", json);
    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "behavior_scene",
        .path = rel_path,
    });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());
    const auto* scene_data = assets.scenes().get_scene_data(handle);
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].behavior.has_value());
    EXPECT_EQ(scene_data->nodes[0].behavior->module, "gameplay");
    EXPECT_EQ(scene_data->nodes[0].behavior->name, "bounce_on_collision");
    EXPECT_TRUE(scene_data->nodes[0].behavior->enabled);

    const auto result = wz::engine::assets::instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.behaviors.size(), 1u);

    const std::string exported = wz::json::serialize_json(
        wz::engine::assets::export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"behavior\""), std::string::npos);
    EXPECT_NE(exported.find("\"module\""), std::string::npos);
    EXPECT_NE(exported.find("\"bounce_on_collision\""), std::string::npos);
}
