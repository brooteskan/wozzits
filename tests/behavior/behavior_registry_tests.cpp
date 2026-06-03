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
#include <math/quaternion.h>
#include <scene/scene_graph.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
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

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kMotionEpsilon = 1e-5f;

    wz::math::Quaternion node_local_rotation(
        const SceneInstance& scene,
        RuntimeEntityId entity)
    {
        wz::math::Transform trs{};
        EXPECT_TRUE(wz::math::decompose_trs(
            wz::core::graph::node_data(scene.storage.polytree, entity).local,
            trs));
        return trs.rotation;
    }

    wz::math::Quaternion node_world_rotation(
        const SceneInstance& scene,
        RuntimeEntityId entity)
    {
        wz::math::Transform trs{};
        EXPECT_TRUE(wz::math::decompose_trs(
            wz::core::graph::node_data(scene.storage.polytree, entity).world,
            trs));
        return trs.rotation;
    }

    void expect_same_rotation(
        const wz::math::Quaternion& actual,
        const wz::math::Quaternion& expected,
        float eps = kMotionEpsilon)
    {
        EXPECT_NEAR(
            std::abs(wz::math::dot(
                wz::math::normalize(actual),
                wz::math::normalize(expected))),
            1.0f,
            eps);
    }

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

    struct SceneQueryConfigProbe
    {
        uint32_t calls = 0;
        uint8_t find_player_by_id = 0;
        uint8_t find_terrain_by_name = 0;
        WzBehaviorEntityId player_entity = WZ_INVALID_BEHAVIOR_ENTITY;
        WzBehaviorEntityId terrain_entity = WZ_INVALID_BEHAVIOR_ENTITY;
        uint8_t enabled_value = 0;
        uint8_t enabled_read = 0;
        double speed_value = 0.0;
        uint8_t speed_read = 0;
        char terrain_id[64]{};
        uint32_t terrain_id_required = 0;
        uint8_t terrain_id_read = 0;
        uint8_t missing_read = 1;
    };

    SceneQueryConfigProbe* g_scene_query_config_probe = nullptr;

    void scene_query_config_behavior(
        const WzBehaviorFrameFacts* facts,
        WzBehaviorEntityId,
        void* user)
    {
        auto* probe = static_cast<SceneQueryConfigProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);

        ++probe->calls;
        probe->find_player_by_id = wz_find_entity_by_authored_id(
            facts,
            "player",
            &probe->player_entity);
        probe->find_terrain_by_name = wz_find_entity_by_name(
            facts,
            "Landscape",
            &probe->terrain_entity);
        probe->enabled_read = wz_config_bool(
            facts,
            "enabled",
            &probe->enabled_value);
        probe->speed_read = wz_config_number(
            facts,
            "speed",
            &probe->speed_value);
        probe->terrain_id_read = wz_config_string(
            facts,
            "terrain_id",
            probe->terrain_id,
            sizeof(probe->terrain_id),
            &probe->terrain_id_required);
        probe->missing_read = wz_config_number(
            facts,
            "missing",
            &probe->speed_value);
    }

    uint8_t register_scene_query_config_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_behavior) {
            return 0;
        }
        return api->register_behavior(
            api->user,
            "test",
            "scene_query_config",
            scene_query_config_behavior,
            g_scene_query_config_probe);
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

    struct InputHelperProbe
    {
        uint32_t calls = 0;
        uint8_t w_down = 0;
        uint8_t space_pressed = 0;
        uint8_t escape_released = 0;
        uint8_t left_mouse_down = 0;
        uint8_t right_mouse_pressed = 0;
        uint8_t middle_mouse_released = 0;
        int32_t mouse_x = 0;
        int32_t mouse_y = 0;
        int32_t mouse_dx = 0;
        int32_t mouse_dy = 0;
        uint8_t focused = 0;
        int32_t window_width = 0;
        int32_t window_height = 0;
        uint8_t controller_count = 0;
        uint8_t controller_connected = 0;
        uint8_t controller_connected_pressed = 0;
        float left_axis_x = 0.0f;
        uint8_t controller_button = 0;
        uint8_t controller_button_pressed = 0;
        uint8_t controller_button_released = 0;
        uint8_t invalid_key = 1;
        uint8_t invalid_mouse = 1;
        float invalid_axis = 1.0f;
        uint8_t invalid_controller = 1;
        uint8_t wasd_result = 0;
        WzVec3 wasd_axis{};
        uint8_t wrote_velocity = 0;
    };

    InputHelperProbe* g_input_helper_probe = nullptr;

    void input_helper_event_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<InputHelperProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);

        ++probe->calls;
        if (!wz_is_event(event, WZ_EVENT_FRAME_UPDATE)) {
            return;
        }

        probe->w_down = wz_key_down(facts, WZ_KEY_W);
        probe->space_pressed = wz_key_pressed(facts, WZ_KEY_SPACE);
        probe->escape_released = wz_key_released(facts, WZ_KEY_ESCAPE);
        probe->left_mouse_down =
            wz_mouse_button_down(facts, WZ_MOUSE_BUTTON_LEFT);
        probe->right_mouse_pressed =
            wz_mouse_button_pressed(facts, WZ_MOUSE_BUTTON_RIGHT);
        probe->middle_mouse_released =
            wz_mouse_button_released(facts, WZ_MOUSE_BUTTON_MIDDLE);
        probe->mouse_x = wz_mouse_x(facts);
        probe->mouse_y = wz_mouse_y(facts);
        probe->mouse_dx = wz_mouse_dx(facts);
        probe->mouse_dy = wz_mouse_dy(facts);
        probe->focused = wz_window_focused(facts);
        probe->window_width = wz_window_width(facts);
        probe->window_height = wz_window_height(facts);
        probe->controller_count = wz_controller_count(facts);
        probe->controller_connected = wz_controller_connected(facts, 1u);
        probe->controller_connected_pressed =
            wz_controller_connected_pressed(facts, 1u);
        probe->left_axis_x =
            wz_controller_axis(facts, 1u, WZ_CONTROLLER_AXIS_LEFT_X);
        probe->controller_button =
            wz_controller_button_down(
                facts,
                1u,
                WZ_CONTROLLER_BUTTON_DPAD_LEFT);
        probe->controller_button_pressed =
            wz_controller_button_pressed(
                facts,
                1u,
                WZ_CONTROLLER_BUTTON_DPAD_RIGHT);
        probe->controller_button_released =
            wz_controller_button_released(
                facts,
                1u,
                WZ_CONTROLLER_BUTTON_START);
        probe->invalid_key = wz_key_down(facts, 999u);
        probe->invalid_mouse = wz_mouse_button_down(facts, 9u);
        probe->invalid_axis = wz_controller_axis(facts, 1u, 99u);
        probe->invalid_controller = wz_controller_connected(facts, 99u);
        probe->wasd_result = wz_input_wasd_axis(facts, &probe->wasd_axis);
        probe->wrote_velocity =
            wz_self_set_linear_velocity(
                facts,
                event,
                probe->wasd_axis.x,
                probe->wasd_axis.y,
                probe->wasd_axis.z);
    }

    uint8_t register_input_helper_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module || !g_input_helper_probe) {
            return 0;
        }

        return api->register_module(
            api->user,
            "input_helper_test",
            input_helper_event_handler,
            g_input_helper_probe);
    }

    struct TransformCommandProbe
    {
        uint32_t calls = 0;
        uint8_t wrote_set_scale = 0;
        uint8_t wrote_add_scale = 0;
        uint8_t wrote_set_rotation = 0;
        uint8_t wrote_set_world_translation = 0;
        uint8_t wrote_add_world_translation = 0;
        uint8_t wrote_other_set_world_translation = 0;
        uint8_t wrote_other_add_world_translation = 0;
        uint8_t wrote_set_linear_velocity = 0;
        float delta_seconds = -1.0f;
        uint64_t frame_index = UINT64_MAX;
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

        probe->delta_seconds = wz_delta_seconds(facts);
        probe->frame_index = wz_frame_index(facts);
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
        probe->wrote_set_world_translation =
            wz_self_set_world_translation(
                facts,
                event,
                10.0f,
                20.0f,
                30.0f);
        probe->wrote_add_world_translation =
            wz_self_add_world_translation(
                facts,
                event,
                1.0f,
                2.0f,
                3.0f);
        probe->wrote_other_set_world_translation =
            wz_other_set_world_translation(
                facts,
                event,
                40.0f,
                50.0f,
                60.0f);
        probe->wrote_other_add_world_translation =
            wz_other_add_world_translation(
                facts,
                event,
                4.0f,
                5.0f,
                6.0f);
        probe->wrote_set_linear_velocity =
            wz_self_set_linear_velocity(
                facts,
                event,
                7.0f,
                8.0f,
                9.0f);
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
        uint8_t vector_self_to_other_result = 0;
        uint8_t distance_self_to_other_result = 0;
        uint8_t direction_self_to_other_result = 0;
        uint8_t null_vector_result = 1;
        uint8_t zero_direction_result = 1;
        WzVec3 self_local_position{};
        WzVec3 self_world_position{};
        WzVec3 other_world_position{};
        WzVec3 vector_self_to_other{};
        WzVec3 direction_self_to_other{};
        WzVec3 zero_direction{};
        float distance_self_to_other = -1.0f;
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
        probe->vector_self_to_other_result =
            wz_vector_self_to_other(
                facts,
                event,
                &probe->vector_self_to_other);
        probe->distance_self_to_other_result =
            wz_distance_self_to_other(
                facts,
                event,
                &probe->distance_self_to_other);
        probe->direction_self_to_other_result =
            wz_direction_self_to_other(
                facts,
                event,
                &probe->direction_self_to_other);
        probe->null_vector_result =
            wz_vector_self_to_other(facts, event, nullptr);
        probe->zero_direction_result =
            wz_direction_between_world_positions(
                facts,
                wz_self(event),
                wz_self(event),
                &probe->zero_direction);
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

    struct SurfaceQueryProbe
    {
        uint32_t calls = 0;
        uint8_t hit_result = 0;
        uint8_t short_range_result = 1;
        uint8_t null_out_result = 1;
        uint8_t zero_direction_result = 1;
        uint8_t away_result = 1;
        uint8_t wrong_entity_result = 1;
        WzSurfaceSample sample{};
    };

    SurfaceQueryProbe* g_surface_query_probe = nullptr;

    void surface_query_event_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<SurfaceQueryProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);

        ++probe->calls;
        if (!wz_is_event(event, WZ_EVENT_COLLISION_ENTER)) {
            return;
        }

        WzVec3 origin{};
        ASSERT_EQ(wz_self_world_position(facts, event, &origin), 1u);
        const WzVec3 down{ .x = 0.0f, .y = -1.0f, .z = 0.0f };
        probe->hit_result = wz_query_collision_surface_ray(
            facts,
            wz_other(event),
            origin,
            down,
            20.0f,
            &probe->sample);
        WzSurfaceSample short_range_sample{};
        probe->short_range_result = wz_query_collision_surface_ray(
            facts,
            wz_other(event),
            origin,
            down,
            5.0f,
            &short_range_sample);
        probe->null_out_result = wz_query_collision_surface_ray(
            facts,
            wz_other(event),
            origin,
            down,
            20.0f,
            nullptr);
        probe->zero_direction_result = wz_query_collision_surface_ray(
            facts,
            wz_other(event),
            origin,
            WzVec3{ .x = 0.0f, .y = 0.0f, .z = 0.0f },
            20.0f,
            &short_range_sample);
        probe->away_result = wz_query_collision_surface_ray(
            facts,
            wz_other(event),
            origin,
            WzVec3{ .x = 0.0f, .y = 1.0f, .z = 0.0f },
            20.0f,
            &short_range_sample);
        probe->wrong_entity_result = wz_query_collision_surface_ray(
            facts,
            WZ_INVALID_BEHAVIOR_ENTITY,
            origin,
            down,
            20.0f,
            &short_range_sample);
    }

    uint8_t register_surface_query_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module || !g_surface_query_probe) {
            return 0;
        }

        return api->register_module(
            api->user,
            "surface_query_test",
            surface_query_event_handler,
            g_surface_query_probe);
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

TEST(BehaviorPluginDynamicModule, LoadsFromCopyAndCleansUpCopy)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    const auto result = plugins.load_dynamic_module(
        registry,
        std::filesystem::path{ WZ_TEST_BEHAVIOR_PLUGIN_DLL });

    ASSERT_TRUE(result.ok()) << result.detail;

#if defined(_WIN32)
    const std::string separator = " -> ";
    const size_t separator_pos = result.detail.find(separator);
    ASSERT_NE(separator_pos, std::string::npos) << result.detail;
    const std::string loaded_path =
        result.detail.substr(separator_pos + separator.size());
    EXPECT_NE(loaded_path.find(".wzload."), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(loaded_path));

    plugins.clear();

    EXPECT_FALSE(std::filesystem::exists(loaded_path));
#else
    plugins.clear();
#endif
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

TEST(BehaviorDispatch, DispatchesRoutedProximityEventsToRegisteredModule)
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
    frame_storage.collision.routed_proximity_entity_events = {
        wz::engine::collision::ProximityEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::ProximityEventKind::Enter,
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
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_PROXIMITY_ENTER);
    EXPECT_EQ(probe.last_kind, WZ_EVENT_PROXIMITY_ENTER);
    EXPECT_EQ(probe.last_entity, 4u);
    EXPECT_EQ(probe.last_other, 9u);
    EXPECT_FALSE(probe.wrote_command)
        << "test handler only writes on collision.enter";

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, DispatchesCollisionBeforeProximityInSameFrame)
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
        },
    };
    frame_storage.collision.routed_proximity_entity_events = {
        wz::engine::collision::ProximityEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::ProximityEventKind::Enter,
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
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_PROXIMITY_ENTER);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
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

TEST(BehaviorModuleApi, InputHelpersReadFrameSnapshotAndWriteVelocity)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    InputHelperProbe probe{};
    g_input_helper_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_input_helper_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "input_helper_test",
        "");
    wz::engine::FrameContext frame_context{};
    frame_context.input.keyboard.down[WZ_KEY_W] = true;
    frame_context.input.keyboard.down[WZ_KEY_D] = true;
    frame_context.input.keyboard.pressed[WZ_KEY_SPACE] = true;
    frame_context.input.keyboard.released[WZ_KEY_ESCAPE] = true;
    frame_context.input.mouse.x = 320;
    frame_context.input.mouse.y = 240;
    frame_context.input.mouse.dx = -3;
    frame_context.input.mouse.dy = 5;
    frame_context.input.mouse.down[WZ_MOUSE_BUTTON_LEFT] = true;
    frame_context.input.mouse.pressed[WZ_MOUSE_BUTTON_RIGHT] = true;
    frame_context.input.mouse.released[WZ_MOUSE_BUTTON_MIDDLE] = true;
    frame_context.input.window.focused = true;
    frame_context.input.window.width = 1280;
    frame_context.input.window.height = 720;
    frame_context.input.controllers.count = 4u;
    auto& controller =
        frame_context.input.controllers.controllers[1];
    controller.connected = true;
    controller.connected_pressed = true;
    controller.axes[WZ_CONTROLLER_AXIS_LEFT_X] = 0.25f;
    controller.buttons[WZ_CONTROLLER_BUTTON_DPAD_LEFT] = true;
    controller.buttons_pressed[WZ_CONTROLLER_BUTTON_DPAD_RIGHT] = true;
    controller.buttons_released[WZ_CONTROLLER_BUTTON_START] = true;

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.w_down, 1u);
    EXPECT_EQ(probe.space_pressed, 1u);
    EXPECT_EQ(probe.escape_released, 1u);
    EXPECT_EQ(probe.left_mouse_down, 1u);
    EXPECT_EQ(probe.right_mouse_pressed, 1u);
    EXPECT_EQ(probe.middle_mouse_released, 1u);
    EXPECT_EQ(probe.mouse_x, 320);
    EXPECT_EQ(probe.mouse_y, 240);
    EXPECT_EQ(probe.mouse_dx, -3);
    EXPECT_EQ(probe.mouse_dy, 5);
    EXPECT_EQ(probe.focused, 1u);
    EXPECT_EQ(probe.window_width, 1280);
    EXPECT_EQ(probe.window_height, 720);
    EXPECT_EQ(probe.controller_count, 4u);
    EXPECT_EQ(probe.controller_connected, 1u);
    EXPECT_EQ(probe.controller_connected_pressed, 1u);
    EXPECT_FLOAT_EQ(probe.left_axis_x, 0.25f);
    EXPECT_EQ(probe.controller_button, 1u);
    EXPECT_EQ(probe.controller_button_pressed, 1u);
    EXPECT_EQ(probe.controller_button_released, 1u);
    EXPECT_EQ(probe.invalid_key, 0u);
    EXPECT_EQ(probe.invalid_mouse, 0u);
    EXPECT_FLOAT_EQ(probe.invalid_axis, 0.0f);
    EXPECT_EQ(probe.invalid_controller, 0u);
    EXPECT_EQ(probe.wasd_result, 1u);
    EXPECT_NEAR(probe.wasd_axis.x, 0.70710677f, 1e-6f);
    EXPECT_FLOAT_EQ(probe.wasd_axis.y, 0.0f);
    EXPECT_NEAR(probe.wasd_axis.z, 0.70710677f, 1e-6f);
    EXPECT_EQ(probe.wrote_velocity, 1u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::SetLinearVelocity);
    EXPECT_NEAR(
        frame_storage.behavior_commands.commands[0].values[0],
        0.70710677f,
        1e-6f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[0].values[1], 0.0f);
    EXPECT_NEAR(
        frame_storage.behavior_commands.commands[0].values[2],
        0.70710677f,
        1e-6f);

    g_input_helper_probe = nullptr;
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
    EXPECT_EQ(probe.vector_self_to_other_result, 1u);
    EXPECT_EQ(probe.distance_self_to_other_result, 1u);
    EXPECT_EQ(probe.direction_self_to_other_result, 1u);
    EXPECT_EQ(probe.null_vector_result, 0u);
    EXPECT_EQ(probe.zero_direction_result, 0u);
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
    EXPECT_FLOAT_EQ(probe.vector_self_to_other.x, 0.0f);
    EXPECT_FLOAT_EQ(probe.vector_self_to_other.y, -2.0f);
    EXPECT_FLOAT_EQ(probe.vector_self_to_other.z, -3.0f);
    EXPECT_FLOAT_EQ(probe.distance_self_to_other, std::sqrt(13.0f));
    EXPECT_FLOAT_EQ(probe.direction_self_to_other.x, 0.0f);
    EXPECT_FLOAT_EQ(
        probe.direction_self_to_other.y,
        -2.0f / std::sqrt(13.0f));
    EXPECT_FLOAT_EQ(
        probe.direction_self_to_other.z,
        -3.0f / std::sqrt(13.0f));
    EXPECT_FLOAT_EQ(probe.zero_direction.x, 0.0f);
    EXPECT_FLOAT_EQ(probe.zero_direction.y, 0.0f);
    EXPECT_FLOAT_EQ(probe.zero_direction.z, 0.0f);

    g_transform_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQuerySamplesTerrainMeshSurface)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_surface_query_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 2.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 2.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "surface_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId terrain_id =
        scene.authored_to_runtime["terrain"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind =
        wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
    surface.occupancy.queryable = true;
    surface.points = {
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 10.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 10.0f, 0.0f, 0.0f } },
    };
    surface.indices = { 0u, 1u, 2u };

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SurfaceQueryProbe probe{};
    g_surface_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_surface_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = terrain_id,
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
    EXPECT_EQ(probe.hit_result, 1u);
    EXPECT_EQ(probe.short_range_result, 0u);
    EXPECT_EQ(probe.null_out_result, 0u);
    EXPECT_EQ(probe.zero_direction_result, 0u);
    EXPECT_EQ(probe.away_result, 0u);
    EXPECT_EQ(probe.wrong_entity_result, 0u);
    EXPECT_EQ(probe.sample.hit, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_NEAR(probe.sample.position.x, 2.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.y, 0.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.z, 2.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.x, 0.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.y, 1.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.z, 0.0f, 1e-5f);

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQueryIgnoresUnqueryableSurfaces)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_surface_query_unqueryable_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 2.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 2.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "surface_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId terrain_id =
        scene.authored_to_runtime["terrain"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind =
        wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
    surface.occupancy.queryable = false;
    surface.points = {
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 10.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 10.0f, 0.0f, 0.0f } },
    };
    surface.indices = { 0u, 1u, 2u };

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SurfaceQueryProbe probe{};
    g_surface_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_surface_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = terrain_id,
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
    EXPECT_EQ(probe.hit_result, 0u);
    EXPECT_EQ(probe.short_range_result, 0u);
    EXPECT_EQ(probe.null_out_result, 0u);

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQueryIgnoresNonTerrainSurfaceShapes)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_surface_query_non_terrain_scene";

    wz::engine::assets::SceneNodeAsset surface_node{};
    surface_node.id = "surface";
    asset.nodes.push_back(std::move(surface_node));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 2.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 2.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "surface_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId surface_id =
        scene.authored_to_runtime["surface"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind = wz::engine::assets::CollisionShapeKind::Bounds;
    surface.occupancy.queryable = true;
    surface.points = {
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 10.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 10.0f, 0.0f, 0.0f } },
    };
    surface.indices = { 0u, 1u, 2u };

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SurfaceQueryProbe probe{};
    g_surface_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_surface_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = surface_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = surface_id,
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
    EXPECT_EQ(probe.hit_result, 0u);

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQueryReturnsNearestMatchingSurfaceHit)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_surface_query_nearest_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset other_terrain{};
    other_terrain.id = "other_terrain";
    asset.nodes.push_back(std::move(other_terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 2.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 2.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "surface_query_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId terrain_id =
        scene.authored_to_runtime["terrain"];
    const RuntimeEntityId other_terrain_id =
        scene.authored_to_runtime["other_terrain"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind =
        wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
    surface.occupancy.queryable = true;
    surface.points = {
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 0.0f, 10.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 10.0f, 0.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 6.0f, 0.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 0.0f, 6.0f, 10.0f } },
        wz::engine::assets::CollisionPoint{ .position = { 10.0f, 6.0f, 0.0f } },
    };
    surface.indices = { 0u, 1u, 2u, 3u, 4u, 5u };

    wz::engine::assets::CollisionAssetData decoy_surface = surface;
    decoy_surface.points[3].position[1] = 9.0f;
    decoy_surface.points[4].position[1] = 9.0f;
    decoy_surface.points[5].position[1] = 9.0f;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SurfaceQueryProbe probe{};
    g_surface_query_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_surface_query_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = other_terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &decoy_surface,
        });
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = terrain_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = terrain_id,
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
    EXPECT_EQ(probe.hit_result, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_NEAR(probe.sample.position.y, 6.0f, 1e-5f)
        << "query should return the nearest triangle on the requested entity";

    g_surface_query_probe = nullptr;
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
    wz::engine::FrameContext frame_context{};
    frame_context.frame.index = 42u;
    frame_context.frame.interval.start = 0u;
    frame_context.frame.interval.end =
        wz::time::TimeSource::ticks_per_second() / 2u;
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.wrote_set_scale, 1u);
    EXPECT_EQ(probe.wrote_add_scale, 1u);
    EXPECT_EQ(probe.wrote_set_rotation, 1u);
    EXPECT_EQ(probe.wrote_set_world_translation, 1u);
    EXPECT_EQ(probe.wrote_add_world_translation, 1u);
    EXPECT_EQ(probe.wrote_other_set_world_translation, 1u);
    EXPECT_EQ(probe.wrote_other_add_world_translation, 1u);
    EXPECT_EQ(probe.wrote_set_linear_velocity, 1u);
    EXPECT_NEAR(probe.delta_seconds, 0.5f, 1e-6f);
    EXPECT_EQ(probe.frame_index, 42u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 8u);
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
    EXPECT_EQ(frame_storage.behavior_commands.commands[3].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[3].kind,
        BehaviorCommandKind::SetWorldTranslation);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[3].values[0], 10.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[3].values[1], 20.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[3].values[2], 30.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[4].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[4].kind,
        BehaviorCommandKind::AddWorldTranslation);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[4].values[0], 1.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[4].values[1], 2.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[4].values[2], 3.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[5].entity, 9u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[5].kind,
        BehaviorCommandKind::SetWorldTranslation);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[5].values[0], 40.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[5].values[1], 50.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[5].values[2], 60.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[6].entity, 9u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[6].kind,
        BehaviorCommandKind::AddWorldTranslation);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[6].values[0], 4.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[6].values[1], 5.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[6].values[2], 6.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[7].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[7].kind,
        BehaviorCommandKind::SetLinearVelocity);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[7].values[0], 7.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[7].values[1], 8.0f);
    EXPECT_FLOAT_EQ(frame_storage.behavior_commands.commands[7].values[2], 9.0f);

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

TEST(BehaviorPluginAbi, SceneQueryAndConfigCallbacksReadAuthoredSceneData)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SceneQueryConfigProbe probe{};
    g_scene_query_config_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_scene_query_config_pack));
    g_scene_query_config_probe = nullptr;

    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_scene_query_config";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    terrain.name = "Landscape";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset player{};
    player.id = "player";
    player.name = "PlayerCube";
    player.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "test",
        .name = "scene_query_config",
        .enabled = true,
        .config = {
            wz::engine::assets::SceneBehaviorConfigValue{
                .key = "enabled",
                .kind = wz::engine::assets::SceneBehaviorConfigValueKind::Bool,
                .bool_value = true,
            },
            wz::engine::assets::SceneBehaviorConfigValue{
                .key = "speed",
                .kind =
                    wz::engine::assets::SceneBehaviorConfigValueKind::Number,
                .number_value = 4.5,
            },
            wz::engine::assets::SceneBehaviorConfigValue{
                .key = "terrain_id",
                .kind =
                    wz::engine::assets::SceneBehaviorConfigValueKind::String,
                .string_value = "terrain",
            },
        },
    };
    asset.nodes.push_back(std::move(player));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);

    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.find_player_by_id, 1u);
    EXPECT_EQ(probe.find_terrain_by_name, 1u);
    EXPECT_EQ(probe.player_entity, scene.authored_to_runtime.at("player"));
    EXPECT_EQ(probe.terrain_entity, scene.authored_to_runtime.at("terrain"));
    EXPECT_EQ(probe.enabled_read, 1u);
    EXPECT_EQ(probe.enabled_value, 1u);
    EXPECT_EQ(probe.speed_read, 1u);
    EXPECT_DOUBLE_EQ(probe.speed_value, 4.5);
    EXPECT_EQ(probe.terrain_id_read, 1u);
    EXPECT_EQ(probe.terrain_id_required, 8u);
    EXPECT_EQ(std::string(probe.terrain_id), "terrain");
    EXPECT_EQ(probe.missing_read, 0u);
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

TEST(BehaviorCommands, SetLinearVelocityCreatesMotionStateAndIntegrates)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_velocity_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_linear_velocity(actor, 2.0f, 4.0f, 6.0f);
    commands.set_angular_velocity(actor, 0.25f, 0.5f, 0.75f);
    commands.set_motion_space(
        actor,
        wz::engine::assets::SceneMotionSpace::Local);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 3u);
    EXPECT_TRUE(changed.empty())
        << "setting velocity updates motion state, not transform state";
    ASSERT_EQ(result.instance.motions.size(), 1u);
    EXPECT_EQ(result.instance.motions[0].node, actor);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[0],
        2.0f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[1],
        4.0f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[2],
        6.0f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[0],
        0.25f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[1],
        0.5f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[2],
        0.75f);
    EXPECT_EQ(
        result.instance.motions[0].component.space,
        wz::engine::assets::SceneMotionSpace::Local);

    const uint32_t integrated =
        integrate_motion(result.instance, 0.5f, &changed);

    EXPECT_EQ(integrated, 2u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 1.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 3.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 1.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 3.0f);
}

TEST(BehaviorCommands, MotionIntegratesLocalLinearVelocityInWorldAxes)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_local_velocity_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.rotation_quat[1] = 0.70710677f;
    node.local.rotation_quat[3] = 0.70710677f;
    node.local.scale[0] = 2.0f;
    node.local.scale[1] = 3.0f;
    node.local.scale[2] = 4.0f;
    node.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 0.0f, 0.0f, 2.0f },
        .space = wz::engine::assets::SceneMotionSpace::Local,
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    const uint32_t integrated =
        integrate_motion(result.instance, 0.5f, &changed);

    EXPECT_EQ(integrated, 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_NEAR(actor_node.world.m[12], 1.0f, 1e-5f);
    EXPECT_NEAR(actor_node.world.m[13], 0.0f, 1e-5f);
    EXPECT_NEAR(actor_node.world.m[14], 0.0f, 1e-5f);
}

TEST(BehaviorCommands, MotionIntegratesLocalVelocityUsingWorldHierarchy)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_local_velocity_hierarchy_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.rotation_quat[2] = 0.70710677f;
    root.local.rotation_quat[3] = 0.70710677f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.local.rotation_quat[1] = 0.70710677f;
    child.local.rotation_quat[3] = 0.70710677f;
    child.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 0.0f, 0.0f, 2.0f },
        .space = wz::engine::assets::SceneMotionSpace::Local,
    };
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    std::vector<RuntimeEntityId> changed;

    const uint32_t integrated =
        integrate_motion(result.instance, 0.5f, &changed);

    EXPECT_EQ(integrated, 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], child_id);
    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_NEAR(child_node.world.m[12], 0.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[13], 1.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[14], 0.0f, 1e-5f);
}

TEST(BehaviorCommands, MotionSpaceCanSwitchBetweenWorldAndLocal)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_motion_space_switch_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.rotation_quat[1] = 0.70710677f;
    node.local.rotation_quat[3] = 0.70710677f;
    node.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 0.0f, 0.0f, 2.0f },
        .space = wz::engine::assets::SceneMotionSpace::World,
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 0.5f, &changed), 1u);
    {
        const auto& actor_node = wz::core::graph::node_data(
            result.instance.storage.polytree,
            actor);
        EXPECT_NEAR(actor_node.world.m[12], 0.0f, 1e-5f);
        EXPECT_NEAR(actor_node.world.m[13], 0.0f, 1e-5f);
        EXPECT_NEAR(actor_node.world.m[14], 1.0f, 1e-5f);
    }

    BehaviorCommandBuffer commands{};
    commands.set_motion_space(
        actor,
        wz::engine::assets::SceneMotionSpace::Local);
    EXPECT_EQ(
        apply_behavior_commands(result.instance, commands.commands, &changed),
        1u);
    EXPECT_TRUE(changed.empty());

    EXPECT_EQ(integrate_motion(result.instance, 0.5f, &changed), 1u);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_NEAR(actor_node.world.m[12], 1.0f, 1e-5f);
    EXPECT_NEAR(actor_node.world.m[13], 0.0f, 1e-5f);
    EXPECT_NEAR(actor_node.world.m[14], 1.0f, 1e-5f);
}

TEST(BehaviorCommands, InvalidMotionSpaceCommandIsIgnored)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_invalid_motion_space_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.motion = wz::engine::assets::SceneMotionAsset{
        .space = wz::engine::assets::SceneMotionSpace::Local,
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    const BehaviorCommand commands[] = {
        BehaviorCommand{
            .entity = actor,
            .kind = BehaviorCommandKind::SetMotionSpace,
            .values = { 2.0f, 0.0f, 0.0f, 0.0f },
        },
        BehaviorCommand{
            .entity = actor,
            .kind = BehaviorCommandKind::SetMotionSpace,
            .values = { -1.0f, 0.0f, 0.0f, 0.0f },
        },
        BehaviorCommand{
            .entity = actor,
            .kind = BehaviorCommandKind::SetMotionSpace,
            .values = {
                std::numeric_limits<float>::quiet_NaN(),
                0.0f,
                0.0f,
                0.0f },
        },
    };

    EXPECT_EQ(apply_behavior_commands(result.instance, commands, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    ASSERT_EQ(result.instance.motions.size(), 1u);
    EXPECT_EQ(
        result.instance.motions[0].component.space,
        wz::engine::assets::SceneMotionSpace::Local);
}

TEST(BehaviorCommands, AngularVelocityCreatesMotionAndIntegrates)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_velocity_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.translation[0] = 3.0f;
    node.local.translation[1] = -2.0f;
    node.local.translation[2] = 5.0f;
    node.local.scale[0] = 2.0f;
    node.local.scale[1] = 3.0f;
    node.local.scale[2] = 4.0f;
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_angular_velocity(actor, 0.0f, 1.5f, 0.0f);
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(
        apply_behavior_commands(result.instance, commands.commands, &changed),
        1u);
    EXPECT_TRUE(changed.empty());
    ASSERT_EQ(result.instance.motions.size(), 1u);
    EXPECT_TRUE(result.instance.motions[0].component.enabled);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[1],
        1.5f);

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 3.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], -2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 5.0f);

    wz::math::Transform trs{};
    ASSERT_TRUE(wz::math::decompose_trs(actor_node.local, trs));
    EXPECT_FLOAT_EQ(trs.scale.x, 2.0f);
    EXPECT_FLOAT_EQ(trs.scale.y, 3.0f);
    EXPECT_FLOAT_EQ(trs.scale.z, 4.0f);
    expect_same_rotation(
        trs.rotation,
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, 1.5f));
}

TEST(BehaviorCommands, AngularVelocityLocalAndWorldComposeDifferently)
{
    wz::engine::assets::SceneAssetData world_asset{};
    world_asset.name = "behavior_angular_world_scene";

    wz::engine::assets::SceneNodeAsset world_node{};
    world_node.id = "actor";
    world_node.local.rotation_quat[1] = 0.70710677f;
    world_node.local.rotation_quat[3] = 0.70710677f;
    world_node.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, 0.0f, kPi * 0.5f },
        .space = wz::engine::assets::SceneMotionSpace::World,
    };
    world_asset.nodes.push_back(std::move(world_node));

    wz::engine::assets::SceneAssetData local_asset = world_asset;
    local_asset.name = "behavior_angular_local_scene";
    local_asset.nodes[0].motion->space =
        wz::engine::assets::SceneMotionSpace::Local;

    auto world_result = wz::engine::assets::instantiate_scene(world_asset);
    ASSERT_TRUE(world_result.ok()) << world_result.error_detail;
    auto local_result = wz::engine::assets::instantiate_scene(local_asset);
    ASSERT_TRUE(local_result.ok()) << local_result.error_detail;

    const RuntimeEntityId world_actor =
        world_result.instance.authored_to_runtime["actor"];
    const RuntimeEntityId local_actor =
        local_result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(world_result.instance, 1.0f, &changed), 1u);
    EXPECT_EQ(integrate_motion(local_result.instance, 1.0f, &changed), 1u);

    const wz::math::Quaternion initial =
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, kPi * 0.5f);
    const wz::math::Quaternion delta =
        wz::math::from_axis_angle({ 0.0f, 0.0f, 1.0f }, kPi * 0.5f);
    expect_same_rotation(
        node_local_rotation(world_result.instance, world_actor),
        wz::math::mul(delta, initial));
    expect_same_rotation(
        node_local_rotation(local_result.instance, local_actor),
        wz::math::mul(initial, delta));
    EXPECT_LT(
        std::abs(wz::math::dot(
            node_local_rotation(world_result.instance, world_actor),
            node_local_rotation(local_result.instance, local_actor))),
        0.999f);
}

TEST(BehaviorCommands, WorldAngularVelocityUsesTrueWorldFrameForChild)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_parented_world_angular_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.rotation_quat[1] = 0.70710677f;
    root.local.rotation_quat[3] = 0.70710677f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, 0.0f, kPi * 0.5f },
        .space = wz::engine::assets::SceneMotionSpace::World,
    };
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], child_id);

    const wz::math::Quaternion parent_rotation =
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, kPi * 0.5f);
    const wz::math::Quaternion delta =
        wz::math::from_axis_angle({ 0.0f, 0.0f, 1.0f }, kPi * 0.5f);
    expect_same_rotation(
        node_world_rotation(result.instance, child_id),
        wz::math::mul(delta, parent_rotation));
}

TEST(BehaviorCommands, AngularVelocitySkipsUnsafeLocalTrs)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_bad_trs_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.scale[0] = 0.0f;
    node.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, kPi, 0.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const auto before = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor).local;
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    const auto& after = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor).local;
    for (uint32_t i = 0; i < 16u; ++i) {
        EXPECT_FLOAT_EQ(after.m[i], before.m[i]) << "index " << i;
        EXPECT_TRUE(std::isfinite(after.m[i])) << "index " << i;
    }
}

TEST(BehaviorCommands, AngularVelocitySkipsShearedLocalTrs)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_sheared_trs_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, kPi, 0.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    auto& node_data = const_cast<wz::scene::TransformNode&>(
        wz::core::graph::node_data(result.instance.storage.polytree, actor));
    node_data.local.m[4] = 0.25f;
    node_data.world = node_data.local;
    const auto before = node_data.local;
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    const auto& after = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor).local;
    for (uint32_t i = 0; i < 16u; ++i) {
        EXPECT_FLOAT_EQ(after.m[i], before.m[i]) << "index " << i;
        EXPECT_TRUE(std::isfinite(after.m[i])) << "index " << i;
    }
}

TEST(BehaviorCommands, AngularVelocitySkipsReflectedLocalTrs)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_reflected_trs_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.scale[0] = -1.0f;
    node.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, kPi, 0.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const auto before = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor).local;
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    const auto& after = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor).local;
    for (uint32_t i = 0; i < 16u; ++i) {
        EXPECT_FLOAT_EQ(after.m[i], before.m[i]) << "index " << i;
        EXPECT_TRUE(std::isfinite(after.m[i])) << "index " << i;
    }
}

TEST(BehaviorCommands, WorldAngularVelocitySkipsDegenerateParentTrs)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_bad_parent_trs_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.scale[0] = 0.0f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, 0.0f, kPi },
        .space = wz::engine::assets::SceneMotionSpace::World,
    };
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    const auto before = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id).local;
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    const auto& after = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id).local;
    for (uint32_t i = 0; i < 16u; ++i) {
        EXPECT_FLOAT_EQ(after.m[i], before.m[i]) << "index " << i;
        EXPECT_TRUE(std::isfinite(after.m[i])) << "index " << i;
    }
}

TEST(BehaviorCommands, AngularVelocitySmallStepsStayCloseToSingleStep)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_angular_stability_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.motion = wz::engine::assets::SceneMotionAsset{
        .angular_velocity = { 0.0f, kPi * 0.5f, 0.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto single = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(single.ok()) << single.error_detail;
    auto stepped = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(stepped.ok()) << stepped.error_detail;

    const RuntimeEntityId single_actor =
        single.instance.authored_to_runtime["actor"];
    const RuntimeEntityId stepped_actor =
        stepped.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(single.instance, 1.0f, &changed), 1u);
    for (uint32_t i = 0; i < 1000u; ++i) {
        ASSERT_EQ(integrate_motion(stepped.instance, 0.001f, &changed), 1u);
    }

    expect_same_rotation(
        node_local_rotation(stepped.instance, stepped_actor),
        node_local_rotation(single.instance, single_actor),
        1e-4f);
}

TEST(BehaviorCommands, LinearAndAngularVelocityIntegrateInSameFrame)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_linear_angular_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 2.0f, 0.0f, 0.0f },
        .angular_velocity = { 0.0f, kPi * 0.5f, 0.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 1.0f, &changed), 2u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 0.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 0.0f);
    expect_same_rotation(
        node_local_rotation(result.instance, actor),
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, kPi * 0.5f));
}

TEST(BehaviorCommands, LocalMotionUsesFallbackAxisForDegenerateScale)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_degenerate_local_motion_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.scale[0] = 0.0f;
    node.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 2.0f, 0.0f, 0.0f },
        .space = wz::engine::assets::SceneMotionSpace::Local,
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 0.5f, &changed), 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], actor);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 1.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 0.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 0.0f);
}

TEST(BehaviorCommands, LinearVelocityIntegratesWorldSpaceForParentedNode)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_velocity_parented_scene";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.local.translation[0] = 10.0f;
    asset.nodes.push_back(std::move(root));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 0.0f, 2.0f, 0.0f },
    };
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    std::vector<RuntimeEntityId> changed;

    const uint32_t integrated =
        integrate_motion(result.instance, 0.25f, &changed);

    EXPECT_EQ(integrated, 1u);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], child_id);
    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_FLOAT_EQ(child_node.local.m[12], 0.0f);
    EXPECT_FLOAT_EQ(child_node.local.m[13], 0.5f);
    EXPECT_FLOAT_EQ(child_node.local.m[14], 0.0f);
    EXPECT_FLOAT_EQ(child_node.world.m[12], 10.0f);
    EXPECT_FLOAT_EQ(child_node.world.m[13], 0.5f);
    EXPECT_FLOAT_EQ(child_node.world.m[14], 0.0f);
}

TEST(BehaviorCommands, LinearVelocityIgnoresNonPositiveOrInvalidDelta)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_velocity_delta_guard_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.motion = wz::engine::assets::SceneMotionAsset{
        .linear_velocity = { 2.0f, 3.0f, 4.0f },
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    std::vector<RuntimeEntityId> changed;

    EXPECT_EQ(integrate_motion(result.instance, 0.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    EXPECT_EQ(integrate_motion(result.instance, -1.0f, &changed), 0u);
    EXPECT_TRUE(changed.empty());
    EXPECT_EQ(
        integrate_motion(
            result.instance,
            std::numeric_limits<float>::infinity(),
            &changed),
        0u);
    EXPECT_TRUE(changed.empty());

    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 0.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 0.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 0.0f);
}

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

TEST(BehaviorCommands, ApplySetWorldTranslationConvertsThroughParentTransform)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_apply_world_translation_scene";

    wz::engine::assets::SceneNodeAsset parent{};
    parent.id = "parent";
    parent.local.translation[0] = 10.0f;
    parent.local.translation[1] = 2.0f;
    parent.local.scale[0] = 2.0f;
    parent.local.scale[1] = 3.0f;
    parent.local.scale[2] = 4.0f;
    asset.nodes.push_back(std::move(parent));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "parent";
    child.local.translation[0] = 1.0f;
    child.local.translation[1] = 1.0f;
    child.local.translation[2] = 1.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.set_world_translation(child_id, 14.0f, 11.0f, 20.0f);
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
    EXPECT_NEAR(child_node.local.m[12], 2.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[13], 3.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[14], 5.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[12], 14.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[13], 11.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[14], 20.0f, 1e-5f);
}

TEST(BehaviorCommands, ApplyAddWorldTranslationPreservesParentedWorldDelta)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_add_world_translation_scene";

    wz::engine::assets::SceneNodeAsset parent{};
    parent.id = "parent";
    parent.local.translation[0] = 10.0f;
    parent.local.scale[0] = 2.0f;
    parent.local.scale[1] = 2.0f;
    parent.local.scale[2] = 2.0f;
    asset.nodes.push_back(std::move(parent));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "parent";
    child.local.translation[0] = 1.0f;
    child.local.translation[1] = 2.0f;
    child.local.translation[2] = 3.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.add_world_translation(child_id, 0.0f, 6.0f, 0.0f);

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        nullptr);

    EXPECT_EQ(applied, 1u);
    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_NEAR(child_node.local.m[12], 1.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[13], 5.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[14], 3.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[12], 12.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[13], 10.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[14], 6.0f, 1e-5f);
}

TEST(BehaviorCommands, ApplySetWorldTranslationUpdatesRootLocalTranslation)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_root_world_translation_scene";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 1.0f;
    actor.local.translation[1] = 2.0f;
    actor.local.translation[2] = 3.0f;
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor_id =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.set_world_translation(actor_id, 7.0f, 8.0f, 9.0f);

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        nullptr);

    EXPECT_EQ(applied, 1u);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor_id);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 7.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 8.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 9.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[12], 7.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[13], 8.0f);
    EXPECT_FLOAT_EQ(actor_node.world.m[14], 9.0f);
}

TEST(BehaviorCommands, ApplySetWorldTranslationHandlesRotatedParent)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_rotated_parent_world_translation_scene";

    wz::engine::assets::SceneNodeAsset parent{};
    parent.id = "parent";
    parent.local.translation[0] = 10.0f;
    parent.local.rotation_quat[2] = 0.70710677f;
    parent.local.rotation_quat[3] = 0.70710677f;
    asset.nodes.push_back(std::move(parent));

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "parent";
    child.local.translation[0] = 1.0f;
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId child_id =
        result.instance.authored_to_runtime["child"];
    BehaviorCommandBuffer commands{};
    commands.set_world_translation(child_id, 8.0f, 3.0f, 4.0f);

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        nullptr);

    EXPECT_EQ(applied, 1u);
    const auto& child_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        child_id);
    EXPECT_NEAR(child_node.local.m[12], 3.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[13], 2.0f, 1e-5f);
    EXPECT_NEAR(child_node.local.m[14], 4.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[12], 8.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[13], 3.0f, 1e-5f);
    EXPECT_NEAR(child_node.world.m[14], 4.0f, 1e-5f);
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
        "enabled": true,
        "config": {
          "terrain_id": "terrain",
          "speed": 4.5,
          "snap_to_ground": true
        }
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
    ASSERT_EQ(scene_data->nodes[0].behavior->config.size(), 3u);
    EXPECT_EQ(scene_data->nodes[0].behavior->config[0].key, "terrain_id");
    EXPECT_EQ(
        scene_data->nodes[0].behavior->config[0].kind,
        wz::engine::assets::SceneBehaviorConfigValueKind::String);
    EXPECT_EQ(
        scene_data->nodes[0].behavior->config[0].string_value,
        "terrain");
    EXPECT_EQ(scene_data->nodes[0].behavior->config[1].key, "speed");
    EXPECT_EQ(
        scene_data->nodes[0].behavior->config[1].kind,
        wz::engine::assets::SceneBehaviorConfigValueKind::Number);
    EXPECT_DOUBLE_EQ(
        scene_data->nodes[0].behavior->config[1].number_value,
        4.5);
    EXPECT_EQ(scene_data->nodes[0].behavior->config[2].key, "snap_to_ground");
    EXPECT_EQ(
        scene_data->nodes[0].behavior->config[2].kind,
        wz::engine::assets::SceneBehaviorConfigValueKind::Bool);
    EXPECT_TRUE(scene_data->nodes[0].behavior->config[2].bool_value);

    const auto result = wz::engine::assets::instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.behaviors.size(), 1u);
    ASSERT_EQ(result.instance.behaviors[0].component.config.size(), 3u);

    const std::string exported = wz::json::serialize_json(
        wz::engine::assets::export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"behavior\""), std::string::npos);
    EXPECT_NE(exported.find("\"config\""), std::string::npos);
    EXPECT_NE(exported.find("\"module\""), std::string::npos);
    EXPECT_NE(exported.find("\"bounce_on_collision\""), std::string::npos);
    EXPECT_NE(exported.find("\"terrain_id\""), std::string::npos);
    EXPECT_NE(exported.find("\"speed\""), std::string::npos);
    EXPECT_NE(exported.find("\"snap_to_ground\""), std::string::npos);
}

TEST(BehaviorDispatch, SceneBehaviorJsonAcceptsEventModuleWithoutName)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_behavior_scene_json_event_module");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "behavior_scene",
  "nodes": [
    {
      "id": "actor",
      "behavior": {
        "module": "test_behavior",
        "name": "",
        "enabled": true
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path rel_path =
        write_text(root, "behavior_event_module.scene.json", json);
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
    EXPECT_EQ(scene_data->nodes[0].behavior->module, "test_behavior");
    EXPECT_TRUE(scene_data->nodes[0].behavior->name.empty());
    EXPECT_TRUE(scene_data->nodes[0].behavior->enabled);
}
