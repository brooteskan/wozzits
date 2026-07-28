#include "behavior_test_support.h"

TEST(BehaviorModuleApi, NullEventHelpersReturnSentinels)
{
    EXPECT_EQ(wz_self(nullptr), WZ_INVALID_BEHAVIOR_ENTITY);
    EXPECT_EQ(wz_other(nullptr), WZ_INVALID_BEHAVIOR_ENTITY);
    EXPECT_EQ(wz_event_kind(nullptr), WZ_EVENT_NONE);
    EXPECT_EQ(wz_is_event(nullptr, WZ_EVENT_FRAME_UPDATE), 0u);
    EXPECT_EQ(wz_self_is_trigger(nullptr), 0u);
}

TEST(BehaviorModuleApi, LogInfofFormatsThroughLogCallback)
{
    std::string message;
    WzBehaviorFrameFacts facts{
        .log_user = &message,
        .log_info = [](void* user, const char* text)
        {
            auto* out = static_cast<std::string*>(user);
            ASSERT_NE(out, nullptr);
            ASSERT_NE(text, nullptr);
            *out = text;
        },
    };

    wz_log_infof(
        &facts,
        "controller=%u axis=%u value=%.2f",
        1u,
        WZ_CONTROLLER_AXIS_LEFT_X,
        0.25);

    EXPECT_EQ(message, "controller=1 axis=0 value=0.25");

    message = "unchanged";
    wz_log_infof(nullptr, "ignored");
    wz_log_infof(&facts, nullptr);
    EXPECT_EQ(message, "unchanged");
}

// v39: warn/error are SEPARATE sinks, not a prefix on the info one. A behavior that
// has failed and gone inert must be able to say so above the routine chatter (#306).
TEST(BehaviorModuleApi, WarnAndErrorRouteToTheirOwnSinks)
{
    struct Sinks
    {
        std::string info;
        std::string warn;
        std::string error;
    } sinks;

    // The log_user is shared across all three, so each lambda picks its own field.
    WzBehaviorFrameFacts facts{
        .log_user = &sinks,
        .log_info = [](void* user, const char* text)
        { static_cast<Sinks*>(user)->info = text; },
        .log_warn = [](void* user, const char* text)
        { static_cast<Sinks*>(user)->warn = text; },
        .log_error = [](void* user, const char* text)
        { static_cast<Sinks*>(user)->error = text; },
    };

    wz_log_info(&facts, "routine");
    wz_log_warnf(&facts, "retrying after %d failure(s)", 3);
    wz_log_errorf(&facts, "'%s' failed to load (bad IR)", "hunt_or_refuel");

    EXPECT_EQ(sinks.info, "routine");
    EXPECT_EQ(sinks.warn, "retrying after 3 failure(s)");
    EXPECT_EQ(sinks.error, "'hunt_or_refuel' failed to load (bad IR)");

    // The init facts carry the same three -- on_init is where a behavior most often
    // discovers it cannot do its job at all.
    WzBehaviorInitFacts init{
        .log_user = &sinks,
        .log_error = [](void* user, const char* text)
        { static_cast<Sinks*>(user)->error = text; },
    };
    wz_log_error(&init, "mind_ir parse FAILED");
    EXPECT_EQ(sinks.error, "mind_ir parse FAILED");

    // A host that wires only log_info leaves warn/error null: silent, never a jump
    // through a null pointer.
    WzBehaviorFrameFacts unwired{.log_user = &sinks};
    wz_log_warn(&unwired, "dropped");
    wz_log_errorf(&unwired, "dropped %d", 1);
    wz_log_warn(nullptr, "dropped");
    wz_log_errorf(nullptr, "dropped");
    EXPECT_EQ(sinks.error, "mind_ir parse FAILED");
}

TEST(BehaviorModuleApi, GpuJobHelpersBuildNamedPortDispatch)
{
    struct Probe
    {
        uint32_t calls = 0;
        WzGpuComputeJobDesc job{};
        WzGpuPortValue ports[4]{};
    } probe;

    WzBehaviorFrameFacts facts{
        .gpu_compute_user = &probe,
        .submit_gpu_compute =
            [](void* user,
               const WzGpuComputeJobDesc* job,
               WzGpuWorkId* out_work) -> uint8_t
            {
                auto* probe = static_cast<Probe*>(user);
                if (!probe || !job || !job->ports) {
                    return 0u;
                }
                ++probe->calls;
                probe->job = *job;
                for (uint32_t i = 0; i < job->port_count && i < 4u; ++i) {
                    probe->ports[i] = job->ports[i];
                }
                if (out_work) {
                    out_work->value = 42u;
                }
                return 1u;
            },
    };

    const uint32_t input[4]{ 1u, 2u, 3u, 4u };
    WzGpuJob job{};
    WzGpuWorkId work{};
    ASSERT_EQ(wz_gpu_begin(&job, "test/multiply"), 1u);
    ASSERT_EQ(wz_gpu_set_groups(&job, 2u, 1u, 1u), 1u);
    ASSERT_EQ(wz_gpu_set_request_tag(&job, 99u), 1u);
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
    ASSERT_EQ(wz_gpu_set_u32(&job, "factor", 3u), 1u);

    ASSERT_EQ(wz_gpu_submit(&facts, &job, &work), 1u);

    EXPECT_EQ(probe.calls, 1u);
    EXPECT_STREQ(probe.job.kernel, "test/multiply");
    EXPECT_EQ(probe.job.port_count, 3u);
    EXPECT_EQ(probe.job.group_count_x, 2u);
    EXPECT_EQ(probe.job.request_tag, 99u);
    EXPECT_EQ(work.value, 42u);
    EXPECT_STREQ(probe.ports[0].name, "input");
    EXPECT_EQ(probe.ports[0].kind, WZ_GPU_PORT_STRUCTURED_BUFFER);
    EXPECT_EQ(probe.ports[0].direction, WZ_GPU_PORT_INPUT);
    EXPECT_EQ(probe.ports[0].initial_data, input);
    EXPECT_STREQ(probe.ports[1].name, "output");
    EXPECT_EQ(probe.ports[1].direction, WZ_GPU_PORT_OUTPUT);
    EXPECT_STREQ(probe.ports[2].name, "factor");
    EXPECT_EQ(probe.ports[2].kind, WZ_GPU_PORT_U32);
    EXPECT_EQ(probe.ports[2].u32[0], 3u);
}

TEST(BehaviorModuleApi, GpuJobHelpersRejectInvalidJobs)
{
    WzGpuJob job{};
    EXPECT_EQ(wz_gpu_begin(nullptr, "x"), 0u);
    EXPECT_EQ(wz_gpu_begin(&job, ""), 0u);
    ASSERT_EQ(wz_gpu_begin(&job, "x"), 1u);
    EXPECT_EQ(wz_gpu_set_groups(&job, 0u, 1u, 1u), 0u);
    EXPECT_EQ(
        wz_gpu_set_structured_input(
            &job,
            "bad",
            1u,
            sizeof(uint32_t),
            nullptr,
            sizeof(uint32_t)),
        0u);
    EXPECT_EQ(wz_gpu_submit(nullptr, &job, nullptr), 0u);

    WzBehaviorFrameFacts facts{};
    EXPECT_EQ(wz_gpu_submit(&facts, &job, nullptr), 0u);
}

namespace
{
    wz::engine::assets::CollisionTriangleBounds triangle_bounds(
        const wz::engine::assets::CollisionPoint& a,
        const wz::engine::assets::CollisionPoint& b,
        const wz::engine::assets::CollisionPoint& c)
    {
        wz::engine::assets::CollisionTriangleBounds bounds{};
        for (int axis = 0; axis < 3; ++axis) {
            bounds.min[axis] =
                (std::min)({
                    a.position[axis],
                    b.position[axis],
                    c.position[axis],
                });
            bounds.max[axis] =
                (std::max)({
                    a.position[axis],
                    b.position[axis],
                    c.position[axis],
                });
        }
        return bounds;
    }

    wz::engine::assets::CollisionAssetData gridded_flat_surface(
        uint32_t cells,
        float size)
    {
        wz::engine::assets::CollisionAssetData surface{};
        surface.shape_kind =
            wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
        surface.occupancy.queryable = true;
        surface.bounds_min[0] = 0.0f;
        surface.bounds_min[1] = 0.0f;
        surface.bounds_min[2] = 0.0f;
        surface.bounds_max[0] = size;
        surface.bounds_max[1] = 0.0f;
        surface.bounds_max[2] = size;

        const float step = size / static_cast<float>(cells);
        for (uint32_t z = 0; z <= cells; ++z) {
            for (uint32_t x = 0; x <= cells; ++x) {
                surface.points.push_back(
                    wz::engine::assets::CollisionPoint{
                        .position = {
                            static_cast<float>(x) * step,
                            0.0f,
                            static_cast<float>(z) * step,
                        },
                    });
            }
        }

        const size_t cell_count = static_cast<size_t>(cells) * cells;
        std::vector<std::vector<uint32_t>> cell_triangles(cell_count);
        surface.surface_grid.origin_x = 0.0f;
        surface.surface_grid.origin_z = 0.0f;
        surface.surface_grid.cell_size_x = step;
        surface.surface_grid.cell_size_z = step;
        surface.surface_grid.cells_x = cells;
        surface.surface_grid.cells_z = cells;
        surface.surface_grid.cell_bounds.resize(cell_count);
        for (auto& bounds : surface.surface_grid.cell_bounds) {
            bounds.min[0] = std::numeric_limits<float>::max();
            bounds.min[1] = std::numeric_limits<float>::max();
            bounds.min[2] = std::numeric_limits<float>::max();
            bounds.max[0] = -std::numeric_limits<float>::max();
            bounds.max[1] = -std::numeric_limits<float>::max();
            bounds.max[2] = -std::numeric_limits<float>::max();
        }

        auto point_index = [cells](uint32_t x, uint32_t z)
        {
            return z * (cells + 1u) + x;
        };
        auto add_triangle_to_cell =
            [&](uint32_t cell, uint32_t ia, uint32_t ib, uint32_t ic)
        {
            const uint32_t tri =
                static_cast<uint32_t>(surface.indices.size() / 3u);
            surface.indices.push_back(ia);
            surface.indices.push_back(ib);
            surface.indices.push_back(ic);
            const auto bounds = triangle_bounds(
                surface.points[ia],
                surface.points[ib],
                surface.points[ic]);
            surface.triangle_bounds.push_back(bounds);
            cell_triangles[cell].push_back(tri);
            auto& cell_bounds = surface.surface_grid.cell_bounds[cell];
            for (int axis = 0; axis < 3; ++axis) {
                cell_bounds.min[axis] =
                    (std::min)(cell_bounds.min[axis], bounds.min[axis]);
                cell_bounds.max[axis] =
                    (std::max)(cell_bounds.max[axis], bounds.max[axis]);
            }
        };

        for (uint32_t z = 0; z < cells; ++z) {
            for (uint32_t x = 0; x < cells; ++x) {
                const uint32_t a = point_index(x, z);
                const uint32_t b = point_index(x + 1u, z);
                const uint32_t c = point_index(x, z + 1u);
                const uint32_t d = point_index(x + 1u, z + 1u);
                const uint32_t cell = z * cells + x;
                add_triangle_to_cell(cell, a, c, b);
                add_triangle_to_cell(cell, b, c, d);
            }
        }

        surface.surface_grid.cell_offsets.push_back(0u);
        for (const auto& tris : cell_triangles) {
            surface.surface_grid.cell_triangle_indices.insert(
                surface.surface_grid.cell_triangle_indices.end(),
                tris.begin(),
                tris.end());
            surface.surface_grid.cell_offsets.push_back(
                static_cast<uint32_t>(
                    surface.surface_grid.cell_triangle_indices.size()));
        }
        return surface;
    }
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

TEST(BehaviorModuleApi, GpuSubmitHelperQueuesNamedPortJobForEventEntity)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    GpuSubmitProbe probe{};
    g_gpu_submit_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_gpu_submit_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "gpu_submit_test",
        "");
    scene.behaviors[0].component.events = { "collision.enter" };
    scene.behaviors[0].component.channel_mask =
        wz::engine::behavior::compile_channel_mask(
            scene.behaviors[0].component.events).mask;
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
        .gpu_compute = &frame_storage.behavior_gpu_compute,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.submit_result, 1u);
    EXPECT_EQ(probe.work.value, 1u);
    ASSERT_EQ(frame_storage.behavior_gpu_compute.jobs.size(), 1u);

    const auto& job = frame_storage.behavior_gpu_compute.jobs[0];
    EXPECT_EQ(job.work.value, 1u);
    EXPECT_EQ(job.entity, 4u);
    EXPECT_EQ(job.kernel, "test/multiply");
    EXPECT_EQ(job.request_tag, 1234u);
    EXPECT_EQ(job.group_count_x, 1u);
    ASSERT_EQ(job.ports.size(), 3u);
    EXPECT_EQ(job.ports[0].name, "input");
    EXPECT_EQ(job.ports[0].kind, WZ_GPU_PORT_STRUCTURED_BUFFER);
    EXPECT_EQ(job.ports[0].direction, WZ_GPU_PORT_INPUT);
    EXPECT_EQ(job.ports[0].initial_data.size(), 4u * sizeof(uint32_t));

    std::array<uint32_t, 4> copied_input{};
    std::memcpy(
        copied_input.data(),
        job.ports[0].initial_data.data(),
        job.ports[0].initial_data.size());
    EXPECT_EQ(copied_input, (std::array<uint32_t, 4>{ 8u, 9u, 10u, 11u }));
    EXPECT_EQ(job.ports[1].name, "output");
    EXPECT_EQ(job.ports[1].direction, WZ_GPU_PORT_OUTPUT);
    EXPECT_EQ(job.ports[2].name, "factor");
    EXPECT_EQ(job.ports[2].u32[0], 6u);

    g_gpu_submit_probe = nullptr;
}

TEST(BehaviorModuleApi, DirectGpuComputeRequestQueuesNamedPortJob)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    GpuSubmitProbe probe{};
    g_gpu_submit_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_gpu_submit_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "gpu_submit_test",
        "");
    scene.behaviors[0].component.events = { "gpu.compute.request" };
    scene.behaviors[0].component.channel_mask =
        wz::engine::behavior::compile_channel_mask(
            scene.behaviors[0].component.events).mask;
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
        .gpu_compute = &frame_storage.behavior_gpu_compute,
    };

    dispatch_behavior_event(
        scene,
        registry,
        context,
        BehaviorEvent{
            .kind = WZ_EVENT_GPU_COMPUTE_REQUEST,
            .entity = 4u,
        });

    EXPECT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.submit_result, 1u);
    ASSERT_EQ(frame_storage.behavior_gpu_compute.jobs.size(), 1u);
    EXPECT_EQ(frame_storage.behavior_gpu_compute.jobs[0].entity, 4u);
    EXPECT_EQ(frame_storage.behavior_gpu_compute.jobs[0].kernel, "test/multiply");

    g_gpu_submit_probe = nullptr;
}

TEST(BehaviorModuleApi, GpuComputeCompletionEventRoutesWithActivePayload)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    GpuEventProbe probe{};
    g_gpu_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_gpu_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "gpu_event_test",
        "");
    scene.behaviors[0].component.events = { "gpu.compute.*" };
    scene.behaviors[0].component.channel_mask =
        wz::engine::behavior::compile_channel_mask(
            scene.behaviors[0].component.events).mask;

    wz::engine::FrameStorage frame_storage{};
    wz::engine::behavior::BehaviorGpuPortValue output{};
    output.name = "output";
    output.kind = WZ_GPU_PORT_STRUCTURED_BUFFER;
    output.direction = WZ_GPU_PORT_OUTPUT;
    output.element_count = 4u;
    output.stride_bytes = sizeof(uint32_t);
    const uint32_t values[4]{ 5u, 10u, 15u, 20u };
    const auto* bytes = reinterpret_cast<const std::byte*>(values);
    output.initial_data.assign(bytes, bytes + sizeof(values));
    frame_storage.behavior_gpu_compute.add_event(
        4u,
        WZ_EVENT_GPU_COMPUTE_COMPLETED,
        WzGpuComputeEventPayload{
            .work = WzGpuWorkId{ 77u },
            .status = WZ_GPU_COMPUTE_STATUS_COMPLETED,
            .request_tag = 1234u,
            .output_count = 1u,
        },
        { std::move(output) });

    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
        .gpu_compute = &frame_storage.behavior_gpu_compute,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.last_kind, WZ_EVENT_GPU_COMPUTE_COMPLETED);
    EXPECT_EQ(probe.last_entity, 4u);
    EXPECT_EQ(probe.active, 1u);
    EXPECT_EQ(probe.work.value, 77u);
    EXPECT_EQ(probe.status, WZ_GPU_COMPUTE_STATUS_COMPLETED);
    EXPECT_EQ(probe.request_tag, 1234u);
    EXPECT_EQ(probe.output_count, 1u);
    EXPECT_EQ(probe.output_elements, 4u);
    EXPECT_EQ(probe.output_stride, sizeof(uint32_t));
    EXPECT_EQ(probe.output_bytes, sizeof(values));
    EXPECT_EQ(probe.read_output, 1u);
    EXPECT_EQ(probe.output_values[0], 5u);
    EXPECT_EQ(probe.output_values[1], 10u);
    EXPECT_EQ(probe.output_values[2], 15u);
    EXPECT_EQ(probe.output_values[3], 20u);
    EXPECT_TRUE(frame_storage.behavior_gpu_compute.jobs.empty());
    EXPECT_TRUE(frame_storage.behavior_gpu_compute.events.empty());

    dispatch_behaviors(scene, registry, context);
    EXPECT_EQ(probe.calls, 1u);

    g_gpu_event_probe = nullptr;
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
    subscribe_frame_update(scene, actor_id);

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
    subscribe_frame_update(scene, actor_id);

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
    subscribe_frame_update(scene, actor_id);

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
    subscribe_frame_update(scene, actor_id);

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
    subscribe_frame_update(scene, actor_id);

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

TEST(BehaviorModuleApi, CollisionSurfaceRayQuerySamplesGriddedTranslatedSurface)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_gridded_surface_query_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 14.0f;
    actor.local.translation[1] = 15.0f;
    actor.local.translation[2] = 24.0f;
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
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface =
        gridded_flat_surface(10u, 10.0f);
    wz::math::Mat4 world_from_local = wz::math::Mat4::identity();
    world_from_local.m[0] = 2.0f;
    world_from_local.m[5] = 3.0f;
    world_from_local.m[10] = 2.0f;
    world_from_local.m[12] = 10.0f;
    world_from_local.m[13] = 5.0f;
    world_from_local.m[14] = 20.0f;

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
            .world_from_local = world_from_local,
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
    BehaviorSurfaceRayQueryStats stats{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
        .surface_ray_stats = &stats,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.hit_result, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_NEAR(probe.sample.position.x, 14.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.y, 5.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.z, 24.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.x, 0.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.y, 1.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.z, 0.0f, 1e-5f);
    EXPECT_GT(stats.grid_queries, 0u);

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, CollisionSurfaceRayQueryUsesGridCandidateRejection)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_gridded_surface_query_candidate_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 17.5f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 17.5f;
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
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface =
        gridded_flat_surface(32u, 32.0f);
    const uint32_t total_triangles =
        static_cast<uint32_t>(surface.indices.size() / 3u);

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
    BehaviorSurfaceRayQueryStats stats{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
        .surface_ray_stats = &stats,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    EXPECT_EQ(probe.hit_result, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_GT(stats.grid_queries, 0u);
    EXPECT_LT(stats.triangles_tested, total_triangles / 8u);
    EXPECT_LT(stats.triangle_bounds_tested, total_triangles / 8u);

    g_surface_query_probe = nullptr;
}

TEST(BehaviorModuleApi, TerrainSurfaceSampleSamplesHeightField)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_terrain_height_sample_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 5.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 5.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "terrain_sample_test",
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
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind =
        wz::engine::assets::CollisionShapeKind::TerrainHeightField;
    surface.occupancy.queryable = true;
    surface.origin[0] = 0.0f;
    surface.origin[1] = 0.0f;
    surface.size[0] = 10.0f;
    surface.size[1] = 10.0f;
    surface.resolution_x = 2u;
    surface.resolution_y = 2u;
    surface.base_height = -1.0f;
    surface.vertical_scale = 2.0f;
    surface.height_samples = {
        0.0f, 1.0f,
        2.0f, 3.0f,
    };

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    TerrainSampleProbe probe{};
    g_terrain_sample_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_terrain_sample_pack));

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
    EXPECT_EQ(probe.sample_result, 1u);
    EXPECT_EQ(probe.null_out_result, 0u);
    EXPECT_EQ(probe.wrong_entity_result, 0u);
    EXPECT_EQ(probe.out_of_bounds_result, 0u);
    EXPECT_EQ(probe.out_of_bounds_sample.hit, 0u);
    EXPECT_EQ(probe.sample.hit, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_NEAR(probe.sample.position.x, 5.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.y, 2.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.z, 5.0f, 1e-5f);

    // Heightfield sampling uses smoothed (Catmull-Rom) interpolation with
    // clamped borders. For this 2x2 grid the gradient at the patch center is
    // 1.25x the bilinear slope: dh/dx = 0.25, dh/dz = 0.5.
    const float normal_scale =
        1.0f / std::sqrt(0.25f * 0.25f + 1.0f + 0.5f * 0.5f);
    EXPECT_NEAR(probe.sample.normal.x, -0.25f * normal_scale, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.y, normal_scale, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.z, -0.5f * normal_scale, 1e-5f);

    g_terrain_sample_probe = nullptr;
}

TEST(BehaviorModuleApi, TerrainSurfaceSampleSamplesMeshSurfaceCell)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_terrain_mesh_sample_scene";

    wz::engine::assets::SceneNodeAsset terrain{};
    terrain.id = "terrain";
    asset.nodes.push_back(std::move(terrain));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 4.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 4.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "terrain_sample_test",
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
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface =
        gridded_flat_surface(4u, 8.0f);
    surface.bounds_min[1] = 0.0f;
    surface.bounds_max[1] = 0.0f;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    TerrainSampleProbe probe{};
    g_terrain_sample_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_terrain_sample_pack));

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
    EXPECT_EQ(probe.sample_result, 1u);
    EXPECT_EQ(probe.sample.hit, 1u);
    EXPECT_EQ(probe.sample.surface_entity, terrain_id);
    EXPECT_NEAR(probe.sample.position.x, 4.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.y, 0.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.position.z, 4.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.x, 0.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.y, 1.0f, 1e-5f);
    EXPECT_NEAR(probe.sample.normal.z, 0.0f, 1e-5f);

    g_terrain_sample_probe = nullptr;
}

TEST(BehaviorModuleApi, TerrainSurfaceSampleIgnoresNonTerrainShapes)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_terrain_sample_non_terrain_scene";

    wz::engine::assets::SceneNodeAsset bounds_node{};
    bounds_node.id = "bounds";
    asset.nodes.push_back(std::move(bounds_node));

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.local.translation[0] = 1.0f;
    actor.local.translation[1] = 10.0f;
    actor.local.translation[2] = 1.0f;
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "terrain_sample_test",
        .name = "",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);
    const RuntimeEntityId bounds_id =
        scene.authored_to_runtime["bounds"];
    const RuntimeEntityId actor_id =
        scene.authored_to_runtime["actor"];
    subscribe_frame_update(scene, actor_id);

    wz::engine::assets::CollisionAssetData surface{};
    surface.shape_kind = wz::engine::assets::CollisionShapeKind::Bounds;
    surface.occupancy.queryable = true;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    TerrainSampleProbe probe{};
    g_terrain_sample_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_terrain_sample_pack));

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.world.push_back(
        wz::engine::collision::CollisionWorldEntry{
            .entity = bounds_id,
            .world_from_local = wz::math::Mat4::identity(),
            .enabled = true,
            .resolved = &surface,
        });
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = actor_id,
            .other = bounds_id,
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
    EXPECT_EQ(probe.sample_result, 0u);
    EXPECT_EQ(probe.sample.hit, 0u);

    g_terrain_sample_probe = nullptr;
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
    // "id:name" suffix: resolves to the same entity as the bare id when the name
    // matches, and fails (0) when the name doesn't.
    EXPECT_EQ(probe.find_player_by_id_name, 1u);
    EXPECT_EQ(probe.player_by_id_name, scene.authored_to_runtime.at("player"));
    EXPECT_EQ(probe.find_player_wrong_name, 0u);
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


TEST(BehaviorModuleApi, PlaySoundNamedEncodesNameHashSelector)
{
    struct Probe { WzBehaviorCommand last{}; int count = 0; } probe;
    WzBehaviorFrameFacts facts{
        .command_writer_user = &probe,
        .write_command = [](void* user, const WzBehaviorCommand* cmd) -> uint8_t
        {
            auto* p = static_cast<Probe*>(user);
            p->last = *cmd;
            ++p->count;
            return 1u;
        },
    };

    // Named play encodes the name-select sentinel in v0 and the 32-bit FNV-1a/32
    // name hash as the bit pattern of v1 (full 32 bits preserved through float).
    ASSERT_EQ(wz_write_play_sound_named(&facts, 7u, "Canon_b"), 1u);
    EXPECT_EQ(probe.count, 1);
    EXPECT_EQ(probe.last.entity, 7u);
    EXPECT_EQ(probe.last.kind, WZ_BEHAVIOR_COMMAND_PLAY_SOUND);
    EXPECT_FLOAT_EQ(probe.last.values[0], -2.0f);

    uint32_t decoded = 0u;
    memcpy(&decoded, &probe.last.values[1], sizeof(uint32_t));
    EXPECT_EQ(decoded, wz_play_sound_hash("Canon_b"));

    // wz_write_play_sound_hashed yields the identical encoding.
    probe.count = 0;
    ASSERT_EQ(
        wz_write_play_sound_hashed(&facts, 7u, wz_play_sound_hash("Canon_b")),
        1u);
    uint32_t decoded2 = 0u;
    memcpy(&decoded2, &probe.last.values[1], sizeof(uint32_t));
    EXPECT_EQ(decoded2, decoded);

    // The hash is usable at compile time (zero runtime cost path).
    static_assert(
        wz_play_sound_hash("Canon_b") != 0u, "constexpr name hash");
}

TEST(BehaviorModuleApi, PlaySoundNamedNullFactsIsNoOp)
{
    EXPECT_EQ(wz_write_play_sound_named(nullptr, 1u, "x"), 0u);
    EXPECT_EQ(wz_write_play_sound_hashed(nullptr, 1u, 123u), 0u);
}

TEST(BehaviorModuleApi, SpawnPrefabEncodesNameHashBitsAndOffset)
{
    struct Probe { WzBehaviorCommand last{}; int count = 0; } probe;
    WzBehaviorFrameFacts facts{
        .command_writer_user = &probe,
        .write_command = [](void* user, const WzBehaviorCommand* cmd) -> uint8_t
        {
            auto* p = static_cast<Probe*>(user);
            p->last = *cmd;
            ++p->count;
            return 1u;
        },
    };

    // Spawn-by-name encodes the prefab name's FNV-1a/32 hash as the bit pattern of
    // v0 (full 32 bits preserved through the float slot) and the offset in v1..v3.
    ASSERT_EQ(
        wz_write_spawn_prefab(&facts, 7u, "enemy_tank", 1.0f, 2.0f, 3.0f), 1u);
    EXPECT_EQ(probe.count, 1);
    EXPECT_EQ(probe.last.entity, 7u);
    EXPECT_EQ(probe.last.kind, WZ_BEHAVIOR_COMMAND_SPAWN_PREFAB);

    uint32_t decoded = 0u;
    memcpy(&decoded, &probe.last.values[0], sizeof(uint32_t));
    EXPECT_EQ(decoded, wz_prefab_hash("enemy_tank"));
    EXPECT_FLOAT_EQ(probe.last.values[1], 1.0f);
    EXPECT_FLOAT_EQ(probe.last.values[2], 2.0f);
    EXPECT_FLOAT_EQ(probe.last.values[3], 3.0f);

    // wz_write_spawn_prefab_hashed yields the identical hash encoding.
    probe.count = 0;
    ASSERT_EQ(
        wz_write_spawn_prefab_hashed(
            &facts, 7u, wz_prefab_hash("enemy_tank"), 1.0f, 2.0f, 3.0f),
        1u);
    uint32_t decoded2 = 0u;
    memcpy(&decoded2, &probe.last.values[0], sizeof(uint32_t));
    EXPECT_EQ(decoded2, decoded);

    // The prefab hash is usable at compile time, and matches FNV-1a/32 exactly
    // (the same constants the engine's prefab_name_hash uses). The ABI -> engine
    // BehaviorCommandKind::SpawnPrefab mapping (from_abi_command_kind) is exercised
    // end-to-end by the WozzitsApp_v1 prefab-spawn test, where the command must
    // round-trip through the adapter for the host spawn to fire.
    static_assert(wz_prefab_hash("enemy_tank") != 0u, "constexpr prefab hash");
}

TEST(BehaviorModuleApi, SpawnPrefabNullFactsIsNoOp)
{
    EXPECT_EQ(wz_write_spawn_prefab(nullptr, 1u, "x", 0.f, 0.f, 0.f), 0u);
    EXPECT_EQ(wz_write_spawn_prefab_hashed(nullptr, 1u, 1u, 0.f, 0.f, 0.f), 0u);
}

TEST(BehaviorModuleApi, SetRenderableParamEncodesNameHashBitsAndXYZ)
{
    struct Probe { WzBehaviorCommand last{}; int count = 0; } probe;
    WzBehaviorFrameFacts facts{
        .command_writer_user = &probe,
        .write_command = [](void* user, const WzBehaviorCommand* cmd) -> uint8_t
        {
            auto* p = static_cast<Probe*>(user);
            p->last = *cmd;
            ++p->count;
            return 1u;
        },
    };

    // Set-by-name encodes the constant name's FNV-1a/32 hash as the bit pattern
    // of v0 (full 32 bits preserved through the float slot) and the new x/y/z in
    // v1..v3. Only three components fit alongside the name — a float4 field's
    // fourth component is preserved host-side, not carried.
    ASSERT_EQ(
        wz_write_set_renderable_param_named(
            &facts, 7u, "tint", 0.9f, 0.1f, 0.4f),
        1u);
    EXPECT_EQ(probe.count, 1);
    EXPECT_EQ(probe.last.entity, 7u);
    EXPECT_EQ(probe.last.kind, WZ_BEHAVIOR_COMMAND_SET_RENDERABLE_PARAM);

    uint32_t decoded = 0u;
    memcpy(&decoded, &probe.last.values[0], sizeof(uint32_t));
    EXPECT_EQ(decoded, wz_renderable_param_hash("tint"));
    EXPECT_FLOAT_EQ(probe.last.values[1], 0.9f);
    EXPECT_FLOAT_EQ(probe.last.values[2], 0.1f);
    EXPECT_FLOAT_EQ(probe.last.values[3], 0.4f);

    // The pre-hashed helper yields the identical encoding.
    probe.count = 0;
    ASSERT_EQ(
        wz_write_set_renderable_param(
            &facts, 7u, wz_renderable_param_hash("tint"), 0.9f, 0.1f, 0.4f),
        1u);
    uint32_t decoded2 = 0u;
    memcpy(&decoded2, &probe.last.values[0], sizeof(uint32_t));
    EXPECT_EQ(decoded2, decoded);

    // The name hash is a compile-time constant and shares FNV-1a/32 with the
    // prefab/clip hashes (the same constants the host's renderable_param_name_-
    // hash uses). The ABI -> BehaviorCommandKind::SetRenderableParam mapping is
    // exercised end-to-end by the WozzitsApp_v1 renderable-param test.
    static_assert(
        wz_renderable_param_hash("tint") == wz_prefab_hash("tint"),
        "renderable-param and prefab hashes share FNV-1a/32");
}

TEST(BehaviorModuleApi, SetVisibleEncodesVisibilityIntoValue0)
{
    struct Probe { WzBehaviorCommand last{}; int count = 0; } probe;
    WzBehaviorFrameFacts facts{
        .command_writer_user = &probe,
        .write_command = [](void* user, const WzBehaviorCommand* cmd) -> uint8_t
        {
            auto* p = static_cast<Probe*>(user);
            p->last = *cmd;
            ++p->count;
            return 1u;
        },
    };

    // Hide: kind = SET_NODE_VISIBLE, visibility carried in values[0] (0 = hidden).
    ASSERT_EQ(wz_write_set_visible(&facts, 5u, 0u), 1u);
    EXPECT_EQ(probe.count, 1);
    EXPECT_EQ(probe.last.entity, 5u);
    EXPECT_EQ(probe.last.kind, WZ_BEHAVIOR_COMMAND_SET_NODE_VISIBLE);
    EXPECT_FLOAT_EQ(probe.last.values[0], 0.0f);

    // Show: any nonzero visibility normalizes to 1.
    ASSERT_EQ(wz_write_set_visible(&facts, 5u, 42u), 1u);
    EXPECT_EQ(probe.count, 2);
    EXPECT_FLOAT_EQ(probe.last.values[0], 1.0f);

    // Null facts / no writer -> no-op (mirrors the other write verbs).
    EXPECT_EQ(wz_write_set_visible(nullptr, 5u, 1u), 0u);
}

TEST(BehaviorModuleApi, SetActiveEncodesActiveIntoValue0)
{
    struct Probe { WzBehaviorCommand last{}; int count = 0; } probe;
    WzBehaviorFrameFacts facts{
        .command_writer_user = &probe,
        .write_command = [](void* user, const WzBehaviorCommand* cmd) -> uint8_t
        {
            auto* p = static_cast<Probe*>(user);
            p->last = *cmd;
            ++p->count;
            return 1u;
        },
    };

    // Park: kind = SET_NODE_ACTIVE, active carried in values[0] (0 = parked).
    ASSERT_EQ(wz_write_set_active(&facts, 5u, 0u), 1u);
    EXPECT_EQ(probe.count, 1);
    EXPECT_EQ(probe.last.entity, 5u);
    EXPECT_EQ(probe.last.kind, WZ_BEHAVIOR_COMMAND_SET_NODE_ACTIVE);
    EXPECT_FLOAT_EQ(probe.last.values[0], 0.0f);

    // Unpark: any nonzero active normalizes to 1.
    ASSERT_EQ(wz_write_set_active(&facts, 5u, 42u), 1u);
    EXPECT_EQ(probe.count, 2);
    EXPECT_FLOAT_EQ(probe.last.values[0], 1.0f);

    // Null facts / no writer -> no-op (mirrors the other write verbs).
    EXPECT_EQ(wz_write_set_active(nullptr, 5u, 1u), 0u);
}

TEST(BehaviorModuleApi, SetRenderableParamNullFactsIsNoOp)
{
    EXPECT_EQ(
        wz_write_set_renderable_param(nullptr, 1u, 1u, 0.f, 0.f, 0.f), 0u);
    EXPECT_EQ(
        wz_write_set_renderable_param_named(nullptr, 1u, "x", 0.f, 0.f, 0.f),
        0u);
}

TEST(BehaviorModuleApi, SetGrainParamHelpersEncodeParamValueRamp)
{
    struct Probe { WzBehaviorCommand last{}; int count = 0; } probe;
    WzBehaviorFrameFacts facts{
        .command_writer_user = &probe,
        .write_command = [](void* user, const WzBehaviorCommand* cmd) -> uint8_t
        {
            auto* p = static_cast<Probe*>(user);
            p->last = *cmd;
            ++p->count;
            return 1u;
        },
    };

    // Typed density helper: values = { param ordinal, value, ramp }.
    ASSERT_EQ(wz_write_set_grain_density(&facts, 9u, 12.5f, 256.0f), 1u);
    EXPECT_EQ(probe.last.entity, 9u);
    EXPECT_EQ(probe.last.kind, WZ_BEHAVIOR_COMMAND_SET_GRAIN_PARAM);
    EXPECT_FLOAT_EQ(probe.last.values[0],
                    static_cast<float>(WZ_GRAIN_PARAM_DENSITY));
    EXPECT_FLOAT_EQ(probe.last.values[1], 12.5f);
    EXPECT_FLOAT_EQ(probe.last.values[2], 256.0f);

    // Generic helper with an explicit param ordinal.
    ASSERT_EQ(
        wz_write_set_grain_param(&facts, 9u, WZ_GRAIN_PARAM_POSITION, 0.25f, 0.0f),
        1u);
    EXPECT_FLOAT_EQ(probe.last.values[0],
                    static_cast<float>(WZ_GRAIN_PARAM_POSITION));
    EXPECT_FLOAT_EQ(probe.last.values[1], 0.25f);

    // Blend helpers carry the blend ordinals + value + ramp.
    ASSERT_EQ(wz_write_set_grain_blend_rate(&facts, 9u, 0.02f, 0.0f), 1u);
    EXPECT_FLOAT_EQ(probe.last.values[0],
                    static_cast<float>(WZ_GRAIN_PARAM_BLEND_RATE));
    EXPECT_FLOAT_EQ(probe.last.values[1], 0.02f);

    ASSERT_EQ(wz_write_set_grain_blend_depth(&facts, 9u, 1.0f, 4800.0f), 1u);
    EXPECT_FLOAT_EQ(probe.last.values[0],
                    static_cast<float>(WZ_GRAIN_PARAM_BLEND_DEPTH));
    EXPECT_FLOAT_EQ(probe.last.values[1], 1.0f);
    EXPECT_FLOAT_EQ(probe.last.values[2], 4800.0f);

    // Null facts => no-op for both forms.
    EXPECT_EQ(
        wz_write_set_grain_param(nullptr, 1u, WZ_GRAIN_PARAM_GAIN, 1.0f, 0.0f),
        0u);
    EXPECT_EQ(wz_write_set_grain_pitch(nullptr, 1u, 1.0f, 0.0f), 0u);
}

TEST(BehaviorModuleApi, SetGrainSourceWeightCarriesIndexInValues3)
{
    struct Probe { WzBehaviorCommand last{}; int count = 0; } probe;
    WzBehaviorFrameFacts facts{
        .command_writer_user = &probe,
        .write_command = [](void* user, const WzBehaviorCommand* cmd) -> uint8_t
        {
            auto* p = static_cast<Probe*>(user);
            p->last = *cmd;
            ++p->count;
            return 1u;
        },
    };

    // Source-weight helper: values = { SOURCE_WEIGHT, weight, ramp, index }.
    ASSERT_EQ(wz_write_set_grain_source_weight(&facts, 5u, 3u, 0.75f, 480.0f), 1u);
    EXPECT_EQ(probe.last.entity, 5u);
    EXPECT_EQ(probe.last.kind, WZ_BEHAVIOR_COMMAND_SET_GRAIN_PARAM);
    EXPECT_FLOAT_EQ(probe.last.values[0],
                    static_cast<float>(WZ_GRAIN_PARAM_SOURCE_WEIGHT));
    EXPECT_FLOAT_EQ(probe.last.values[1], 0.75f);
    EXPECT_FLOAT_EQ(probe.last.values[2], 480.0f);
    EXPECT_FLOAT_EQ(probe.last.values[3], 3.0f);   // the clip index

    EXPECT_EQ(wz_write_set_grain_source_weight(nullptr, 1u, 0u, 1.0f, 0.0f), 0u);
}

TEST(BehaviorModuleApi, GrainProgramFansOutPerParamCommands)
{
    struct Probe { std::vector<WzBehaviorCommand> cmds; } probe;
    WzBehaviorFrameFacts facts{
        .command_writer_user = &probe,
        .write_command = [](void* user, const WzBehaviorCommand* cmd) -> uint8_t
        {
            static_cast<Probe*>(user)->cmds.push_back(*cmd);
            return 1u;
        },
    };

    WzGrainProgram prog = {};
    prog.weight_count = 4u;
    prog.weights[0] = 1.0f; prog.weights[1] = 0.0f;
    prog.weights[2] = 0.5f; prog.weights[3] = 0.0f;
    prog.gain = 0.8f;
    prog.grain_ms = 500.0f;
    prog.window = WZ_GRAIN_WINDOW_GAUSSIAN;

    ASSERT_EQ(wz_write_grain_program(&facts, 7u, &prog, 2400.0f), 1u);

    // 4 source weights + gain/density/position/pitch + grain_ms/pos_jitter/
    // pitch_jitter/pan_center/pan_spread/window_param + blend_rate/blend_depth +
    // window = 4 + 13.
    ASSERT_EQ(probe.cmds.size(), 17u);
    for (const auto& c : probe.cmds) {
        EXPECT_EQ(c.entity, 7u);
        EXPECT_EQ(c.kind, WZ_BEHAVIOR_COMMAND_SET_GRAIN_PARAM);
    }

    // The source-weight commands carry their index in values[3] + the weight.
    int weight_cmds = 0;
    for (const auto& c : probe.cmds) {
        if (c.values[0] != static_cast<float>(WZ_GRAIN_PARAM_SOURCE_WEIGHT)) {
            continue;
        }
        ++weight_cmds;
        const int idx = static_cast<int>(c.values[3]);
        ASSERT_GE(idx, 0);
        ASSERT_LT(idx, 4);
        EXPECT_FLOAT_EQ(c.values[1], prog.weights[idx]);
        EXPECT_FLOAT_EQ(c.values[2], 2400.0f);   // weights crossfade over the ramp
    }
    EXPECT_EQ(weight_cmds, 4);

    // Window SHAPE snaps (ramp 0); a scalar like gain rides the program ramp.
    for (const auto& c : probe.cmds) {
        if (c.values[0] == static_cast<float>(WZ_GRAIN_PARAM_WINDOW)) {
            EXPECT_FLOAT_EQ(c.values[2], 0.0f);
        }
        if (c.values[0] == static_cast<float>(WZ_GRAIN_PARAM_GAIN)) {
            EXPECT_FLOAT_EQ(c.values[1], 0.8f);
            EXPECT_FLOAT_EQ(c.values[2], 2400.0f);
        }
    }

    // Null program / facts => no-op.
    EXPECT_EQ(wz_write_grain_program(&facts, 7u, nullptr, 1.0f), 0u);
    EXPECT_EQ(wz_write_grain_program(nullptr, 7u, &prog, 1.0f), 0u);
}
