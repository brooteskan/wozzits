#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

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
#include <array>
#include <cmath>
#include <cstring>
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

    void subscribe_frame_update(
        SceneInstance& scene,
        RuntimeEntityId entity);

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
                .events = {
                    "frame.update",
                    "collision.*",
                    "proximity.*",
                    "input.*",
                },
                .channel_mask =
                    wz::engine::behavior::channel_mask_for_token(
                        "frame.update")
                    | wz::engine::behavior::channel_mask_for_token(
                        "collision.*")
                    | wz::engine::behavior::channel_mask_for_token(
                        "proximity.*")
                    | wz::engine::behavior::channel_mask_for_token(
                        "input.*"),
            },
        });
        return scene;
    }

    void subscribe_frame_update(
        SceneInstance& scene,
        RuntimeEntityId entity)
    {
        std::vector<std::string> channels{
            "frame.update",
            "collision.*",
            "proximity.*",
            "input.*",
        };
        const auto compiled =
            wz::engine::behavior::compile_channel_mask(channels);
        for (auto& behavior : scene.behaviors) {
            if (behavior.node == entity) {
                behavior.component.events = std::move(channels);
                behavior.component.channel_mask = compiled.mask;
                return;
            }
        }
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

    struct GpuSubmitProbe
    {
        uint32_t calls = 0;
        uint8_t submit_result = 0;
        WzGpuWorkId work{};
    };

    GpuSubmitProbe* g_gpu_submit_probe = nullptr;

    void gpu_submit_event_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<GpuSubmitProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);

        ++probe->calls;
        if (!wz_is_event(event, WZ_EVENT_COLLISION_ENTER)
            && !wz_is_event(event, WZ_EVENT_GPU_COMPUTE_REQUEST))
        {
            return;
        }

        const uint32_t input[4]{ 8u, 9u, 10u, 11u };
        WzGpuJob job{};
        ASSERT_EQ(wz_gpu_begin(&job, "test/multiply"), 1u);
        ASSERT_EQ(wz_gpu_set_request_tag(&job, 1234u), 1u);
        ASSERT_EQ(
            wz_gpu_set_structured_input(
                &job,
                "input",
                4u,
                sizeof(uint32_t),
                input,
                sizeof(input)),
            1u);
        ASSERT_EQ(
            wz_gpu_set_structured_output(
                &job,
                "output",
                4u,
                sizeof(uint32_t)),
            1u);
        ASSERT_EQ(wz_gpu_set_u32(&job, "factor", 6u), 1u);

        probe->submit_result = wz_gpu_submit(facts, &job, &probe->work);
    }

    uint8_t register_gpu_submit_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module_desc || !g_gpu_submit_probe) {
            return 0;
        }

        static const char* events[] = {
            "collision.enter",
            "gpu.compute.request",
        };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "gpu_submit_test",
            .on_event = gpu_submit_event_handler,
            .event_channels = events,
            .event_channel_count =
                static_cast<uint32_t>(
                    sizeof(events) / sizeof(events[0])),
            .module_user_data = g_gpu_submit_probe,
        };
        return api->register_module_desc(api->user, &desc);
    }

    struct GpuEventProbe
    {
        uint32_t calls = 0;
        WzBehaviorEventKind last_kind = WZ_EVENT_NONE;
        WzBehaviorEntityId last_entity = WZ_INVALID_BEHAVIOR_ENTITY;
        uint8_t active = 0;
        WzGpuWorkId work{};
        WzGpuComputeStatus status = WZ_GPU_COMPUTE_STATUS_NONE;
        uint64_t request_tag = 0u;
        uint32_t output_count = 0u;
        uint32_t output_elements = 0u;
        uint32_t output_stride = 0u;
        uint64_t output_bytes = 0u;
        uint8_t read_output = 0u;
        uint32_t output_values[4]{};
    };

    GpuEventProbe* g_gpu_event_probe = nullptr;

    void gpu_event_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<GpuEventProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);

        ++probe->calls;
        probe->last_kind = wz_event_kind(event);
        probe->last_entity = wz_self(event);
        probe->active = wz_gpu_compute_event_active(facts);
        probe->work = wz_gpu_compute_event_work(facts);
        probe->status = wz_gpu_compute_event_status(facts);
        probe->request_tag = wz_gpu_compute_event_request_tag(facts);
        probe->output_count = wz_gpu_compute_event_output_count(facts);
        probe->output_elements =
            wz_gpu_compute_output_element_count(facts, "output");
        probe->output_stride =
            wz_gpu_compute_output_stride_bytes(facts, "output");
        probe->output_bytes =
            wz_gpu_compute_output_byte_count(facts, "output");
        probe->read_output = 1u;
        for (uint32_t i = 0; i < 4u; ++i) {
            probe->read_output &=
                wz_gpu_compute_read_output_u32(
                    facts,
                    "output",
                    i,
                    &probe->output_values[i]);
        }
    }

    uint8_t register_gpu_event_pack(WzBehaviorPluginApi* api)
    {
        static const char* events[] = {
            "gpu.compute.*",
        };
        if (!api || !api->register_module_desc || !g_gpu_event_probe) {
            return 0;
        }

        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "gpu_event_test",
            .on_event = gpu_event_handler,
            .event_channels = events,
            .event_channel_count =
                static_cast<uint32_t>(
                    sizeof(events) / sizeof(events[0])),
            .module_user_data = g_gpu_event_probe,
        };
        return api->register_module_desc(api->user, &desc);
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

    struct TerrainSampleProbe
    {
        uint32_t calls = 0;
        uint8_t sample_result = 0;
        uint8_t null_out_result = 1;
        uint8_t wrong_entity_result = 1;
        uint8_t out_of_bounds_result = 1;
        WzSurfaceSample sample{};
        WzSurfaceSample out_of_bounds_sample{};
    };

    TerrainSampleProbe* g_terrain_sample_probe = nullptr;

    void terrain_sample_event_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<TerrainSampleProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);

        ++probe->calls;
        if (!wz_is_event(event, WZ_EVENT_COLLISION_ENTER)) {
            return;
        }

        WzVec3 position{};
        ASSERT_EQ(wz_self_world_position(facts, event, &position), 1u);
        probe->sample_result = wz_sample_terrain_surface(
            facts,
            wz_other(event),
            position.x,
            position.z,
            &probe->sample);
        probe->null_out_result = wz_sample_terrain_surface(
            facts,
            wz_other(event),
            position.x,
            position.z,
            nullptr);
        WzSurfaceSample wrong_entity_sample{};
        probe->wrong_entity_result = wz_sample_terrain_surface(
            facts,
            WZ_INVALID_BEHAVIOR_ENTITY,
            position.x,
            position.z,
            &wrong_entity_sample);
        probe->out_of_bounds_result = wz_sample_terrain_surface(
            facts,
            wz_other(event),
            position.x + 1000.0f,
            position.z + 1000.0f,
            &probe->out_of_bounds_sample);
    }

    uint8_t register_terrain_sample_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module || !g_terrain_sample_probe) {
            return 0;
        }

        return api->register_module(
            api->user,
            "terrain_sample_test",
            terrain_sample_event_handler,
            g_terrain_sample_probe);
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

































// ─── Adversarial tests ──────────────────────────────────────────────
















































#if defined(__clang__)
#pragma clang diagnostic pop
#endif
