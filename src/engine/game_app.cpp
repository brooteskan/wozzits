// src/engine/game_app.cpp
#include <event/platform_event.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>
#include <engine/game_app.h>
#include <engine/behavior/behavior_command_apply.h>
#include <engine/behavior/behavior_dispatch.h>
#include <engine/behavior/builtin_behaviors.h>
#include <engine/collision/collision_frame.h>
#include <engine/runtime_camera.h>
#include <math/projection.h>

#include <gpu/scalar_field_texture.h>
#include <gpu/dx12/dx12.h>
#include <engine/assets/scalar_field/scalar_field.h>

#include <stats/scene_render_storage.h>
#include <scene/compile/legacy_classification.h>

#include <engine/game_app_benchmark.h>

#define INIT_FAIL(msg) \
    do { OutputDebugStringA("GameApp init failed: " msg "\n"); return false; } while (0)

namespace wz::app
{
    namespace
    {
        constexpr uint32_t kAnimatedDebugObjectCount = 10;
        constexpr uint32_t kAnimatedSubtreeRootCount = 10;
        constexpr uint32_t kAnimatedSubtreeLeafCount = 10;

        constexpr std::array<DebugSceneConfig, 5> kDebugSceneModes{ {
            DebugSceneConfig{
                .name = "objects-100-static-cull-on",
                .object_count = 100,
                .animation = DebugSceneAnimation::Static,
            },
            DebugSceneConfig{
                .name = "objects-1000-animated-leaves-cull-on",
                .object_count = 1000,
                .animation = DebugSceneAnimation::Leaves,
            },
            DebugSceneConfig{
                .name = "objects-1000-animated-parent-subtree-cull-on",
                .object_count = 1000,
                .animation = DebugSceneAnimation::ParentSubtree,
            },
            DebugSceneConfig{
                .name = "objects-1000-static-cull-off",
                .object_count = 1000,
                .animation = DebugSceneAnimation::Static,
                .culling_disabled = true,
            },
            DebugSceneConfig{
                .name = "objects-100000-static-benchmark-cull-on",
                .object_count = 100000,
                .animation = DebugSceneAnimation::Static,
                .wide_layout = true,
            },
        } };

        constexpr uint32_t kDefaultDebugSceneModeIndex = 1;

        struct PlatformEventsJobData
        {
            wz::engine::Context* ctx = nullptr;
            wz::engine::AppContext* app_ctx = nullptr;
        };

        struct ShutdownInputJobData
        {
            wz::engine::Context* ctx = nullptr;
            const wz::engine::FrameContext* fctx = nullptr;
        };

        struct CameraUpdateJobData
        {
            wz::app::RuntimeCamera* camera = nullptr;
            const wz::engine::FrameContext* fctx = nullptr;
            float dt = 0.0f;
        };

        struct BuildViewJobData
        {
            wz::engine::FrameStorage* frame = nullptr;
            const wz::app::RuntimeCamera* camera = nullptr;
            const wz::engine::FrameContext* fctx = nullptr;
        };

        struct RenderPrepJobData
        {
            wz::engine::FrameStorage* frame = nullptr;
            wz::engine::FrameDirtyState* frame_dirty = nullptr;
            wz::app::DebugObjectRuntime* debug_object = nullptr;
        };

        struct CollisionFrameJobData
        {
            wz::engine::FrameStorage* frame = nullptr;
            const wz::engine::assets::SceneInstance* scene = nullptr;
            const wz::engine::assets::CollisionAssetModule* collisions = nullptr;
        };

        struct InputEventJobData
        {
            wz::engine::FrameStorage* frame = nullptr;
            const wz::engine::FrameContext* fctx = nullptr;
            const wz::engine::assets::SceneInstance* scene = nullptr;
        };

        struct BehaviorDispatchJobData
        {
            wz::engine::FrameStorage* frame = nullptr;
            const wz::engine::FrameContext* fctx = nullptr;
            wz::engine::assets::SceneInstance* scene = nullptr;
            const wz::engine::behavior::BehaviorRegistry* registry = nullptr;
            wz::Logger* logger = nullptr;
        };

        struct BehaviorCommandApplyJobData
        {
            wz::engine::FrameStorage* frame = nullptr;
            const wz::engine::FrameContext* fctx = nullptr;
            wz::engine::FrameDirtyState* frame_dirty = nullptr;
            wz::engine::assets::SceneInstance* scene = nullptr;
            wz::app::DebugObjectRuntime* debug_object = nullptr;
        };

        struct TerrainConstraintJobData
        {
            wz::engine::FrameStorage* frame = nullptr;
            wz::engine::FrameDirtyState* frame_dirty = nullptr;
            wz::engine::assets::SceneInstance* scene = nullptr;
            wz::app::DebugObjectRuntime* debug_object = nullptr;
        };

        void update_world_for_nodes(
            wz::scene::SceneGraph& g,
            std::span<const wz::core::graph::NodeHandle> nodes)
        {
            for (wz::core::graph::NodeHandle n : nodes)
            {
                auto& node = const_cast<wz::scene::TransformNode&>(
                    wz::core::graph::node_data(g, n));

                const wz::core::graph::NodeHandle p =
                    wz::core::graph::parent(g, n);

                node.world =
                    (p == wz::core::graph::INVALID_NODE)
                    ? node.local
                    : wz::scene::compute_world(
                        wz::core::graph::node_data(g, p).world,
                        node.local);
            }
        }

        void set_debug_object_local(
            wz::app::DebugObjectRuntime& dbg,
            wz::core::graph::NodeHandle n,
            const wz::math::Mat4& local)
        {
            if (!dbg.collision_scene_valid)
                return;

            if (n >= wz::core::graph::node_count(dbg.collision_scene.storage.polytree))
                return;

            wz::scene::set_local(
                dbg.collision_scene.storage.polytree,
                n,
                local);
        }

        void update_debug_object_animation(
            wz::app::GameApp& app,
            float t)
        {
            auto& dbg = app.debug_object;

            if (!dbg.ready)
                return;

            dbg.transform_affected_nodes.clear();

            const DebugSceneAnimation animation =
                dbg.config ? dbg.config->animation : DebugSceneAnimation::Static;

            if (animation == DebugSceneAnimation::Leaves)
            {
                const uint32_t count =
                    static_cast<uint32_t>(dbg.animated_nodes.size());

                for (uint32_t i = 0; i < count; ++i)
                {
                    const auto n = dbg.animated_nodes[i];
                    const auto base = dbg.animated_base_positions[i];

                    wz::math::Mat4 local = wz::math::Mat4::identity();

                    local.m[12] = base.x;
                    local.m[13] = base.y + 0.5f * std::sin(t * 2.0f + static_cast<float>(i));
                    local.m[14] = base.z;

                    set_debug_object_local(dbg, n, local);

                    dbg.transform_affected_nodes.push_back(n);
                }
            }
            else if (animation == DebugSceneAnimation::ParentSubtree)
            {
                const uint32_t count =
                    static_cast<uint32_t>(dbg.animated_parent_nodes.size());

                for (uint32_t i = 0; i < count; ++i)
                {
                    const auto n = dbg.animated_parent_nodes[i];
                    const auto base = dbg.animated_parent_base_positions[i];

                    wz::math::Mat4 local = wz::math::Mat4::identity();

                    local.m[12] = base.x;
                    local.m[13] = base.y + 0.75f * std::sin(t * 1.5f + static_cast<float>(i));
                    local.m[14] = base.z;

                    set_debug_object_local(dbg, n, local);

                    dbg.transform_affected_nodes.push_back(n);
                }

                for (wz::core::graph::NodeHandle n : dbg.animated_nodes)
                    dbg.transform_affected_nodes.push_back(n);
            }

            dbg.transforms_dirty = !dbg.transform_affected_nodes.empty();
        }

        uint32_t debug_grid_columns(uint32_t object_count)
        {
            return std::max(
                1u,
                static_cast<uint32_t>(
                    std::ceil(std::sqrt(static_cast<float>(object_count)))));
        }

        wz::math::Vec3 debug_grid_position(
            uint32_t index,
            uint32_t object_count,
            bool wide_layout)
        {
            const uint32_t columns = debug_grid_columns(object_count);
            const uint32_t rows = std::max(
                1u,
                (object_count + columns - 1u) / columns);

            const uint32_t x = index % columns;
            const uint32_t y = index / columns;

            const float spacing = wide_layout ? 1.75f : 1.25f;

            return wz::math::Vec3{
                (static_cast<float>(x) - static_cast<float>(columns) * 0.5f) * spacing,
                (static_cast<float>(y) - static_cast<float>(rows) * 0.5f) * spacing,
                8.0f,
            };
        }

        const char* debug_scene_animation_name(DebugSceneAnimation animation)
        {
            switch (animation)
            {
            case DebugSceneAnimation::Static:
                return "Static";

            case DebugSceneAnimation::Leaves:
                return "Leaves";

            case DebugSceneAnimation::ParentSubtree:
                return "ParentSubtree";

            default:
                return "Unknown";
            }
        }

        bool build_debug_object_scene(
            wz::app::GameApp& app,
            const DebugSceneConfig& config)
        {
            using namespace wz::scene;
            using namespace wz::core::graph;
            using namespace wz::math;

            const uint32_t object_count = config.object_count;

            app.debug_object.config = &config;
            app.debug_object.renderable_count = object_count;
            app.debug_object.animated_parent_nodes.clear();
            app.debug_object.animated_parent_base_positions.clear();
            app.debug_object.animated_nodes.clear();
            app.debug_object.animated_base_positions.clear();
            app.debug_object.transform_affected_nodes.clear();

            SceneBuilder b;

            TransformNode root{};
            root.local = Mat4::identity();

            NodeHandle root_h = add_node(b, root);

            std::vector<NodeHandle> object_nodes;
            object_nodes.reserve(object_count);

            auto add_renderable_object =
                [&](NodeHandle parent_h, const Vec3& local_position) -> NodeHandle
            {
                TransformNode object{};
                object.local = Mat4::identity();

                object.local.m[12] = local_position.x;
                object.local.m[13] = local_position.y;
                object.local.m[14] = local_position.z;

                object.flags = TransformNodeFlag::RenderDomain;
                object.motion_type = TransformNode::MotionType::Static;

                NodeHandle object_h = add_node(b, object);
                add_edge(b, parent_h, object_h);

                object_nodes.push_back(object_h);
                return object_h;
            };

            if (config.animation == DebugSceneAnimation::ParentSubtree)
            {
                const uint32_t parent_count = std::min(
                    kAnimatedSubtreeRootCount,
                    std::max(1u, object_count / kAnimatedSubtreeLeafCount));

                uint32_t next_object = 0;

                for (uint32_t p = 0; p < parent_count && next_object < object_count; ++p)
                {
                    const Vec3 parent_pos =
                        debug_grid_position(
                            p * kAnimatedSubtreeLeafCount,
                            object_count,
                            config.wide_layout);

                    TransformNode parent{};
                    parent.local = Mat4::identity();
                    parent.local.m[12] = parent_pos.x;
                    parent.local.m[13] = parent_pos.y;
                    parent.local.m[14] = parent_pos.z;

                    NodeHandle parent_h = add_node(b, parent);
                    add_edge(b, root_h, parent_h);

                    app.debug_object.animated_parent_nodes.push_back(parent_h);
                    app.debug_object.animated_parent_base_positions.push_back(parent_pos);

                    const uint32_t children = std::min(
                        kAnimatedSubtreeLeafCount,
                        object_count - next_object);

                    for (uint32_t c = 0; c < children; ++c)
                    {
                        const float local_x =
                            (static_cast<float>(c % 5u) - 2.0f) * 0.35f;
                        const float local_y =
                            (static_cast<float>(c / 5u) - 0.5f) * 0.35f;

                        const NodeHandle object_h =
                            add_renderable_object(
                                parent_h,
                                Vec3{ local_x, local_y, 0.0f });

                        app.debug_object.animated_nodes.push_back(object_h);
                        ++next_object;
                    }
                }

                while (next_object < object_count)
                {
                    add_renderable_object(
                        root_h,
                        debug_grid_position(
                            next_object,
                            object_count,
                            config.wide_layout));
                    ++next_object;
                }
            }
            else
            {
                for (uint32_t i = 0; i < object_count; ++i)
                {
                    const Vec3 position =
                        debug_grid_position(i, object_count, config.wide_layout);

                    const NodeHandle object_h =
                        add_renderable_object(root_h, position);

                    if (
                        config.animation == DebugSceneAnimation::Leaves &&
                        i < kAnimatedDebugObjectCount)
                    {
                        app.debug_object.animated_nodes.push_back(object_h);
                        app.debug_object.animated_base_positions.push_back(position);
                    }
                }
            }

            auto scene_result = build(std::move(b));
            if (!scene_result.has_value())
                return false;

            app.debug_object.collision_scene = {};
            app.debug_object.collision_scene.storage = std::move(*scene_result);

            app.debug_object.descriptors.clear();
            app.debug_object.descriptors.resize(
                node_count(app.debug_object.collision_scene.storage.polytree)
            );

            for (auto& desc : app.debug_object.descriptors)
            {
                desc = RenderableDescriptor{
                    .node_class = classify_legacy_renderable(RenderPipeline::None),
                };
            }

            const AABB object_bounds =
                config.culling_disabled
                ? AABB{
                    .min = Vec3{ -10000.0f, -10000.0f, -10000.0f },
                    .max = Vec3{  10000.0f,  10000.0f,  10000.0f },
                }
                : AABB{
                    .min = Vec3{ -0.5f, -0.5f, -0.5f },
                    .max = Vec3{  0.5f,  0.5f,  0.5f },
                };

            for (NodeHandle object_h : object_nodes)
            {
                app.debug_object.descriptors[object_h] = RenderableDescriptor{
                    .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                    .mesh = 0,
                    .material = 0,
                    .local_bounds = object_bounds,
                    .splat_data = {},
                    .visible = true,
                };
            }

            propagate_all(app.debug_object.collision_scene.storage.polytree);

            app.debug_object.ready = true;
            app.debug_object.collision_scene_valid = true;
            app.debug_object.transforms_dirty = false;
            wz::engine::behavior::initialize_behaviors(
                app.debug_object.collision_scene,
                app.behavior_registry,
                &app.ctx.logger);

            return true;
        }

        bool set_debug_scene_mode(
            wz::app::GameApp& app,
            uint32_t mode_index)
        {
            if (mode_index >= kDebugSceneModes.size())
                return false;

            if (
                app.debug_object.ready &&
                app.debug_object.mode_index == mode_index)
            {
                return true;
            }

            app.debug_object.ready = false;
            app.debug_object.collision_scene_valid = false;
            app.debug_object.compiled_scene_valid = false;
            app.debug_object.transforms_dirty = false;
            app.debug_object.animation_time_seconds = 0.0f;
            app.frame_dirty.mark_render_full_compile();

            const DebugSceneConfig& config = kDebugSceneModes[mode_index];

            if (!build_debug_object_scene(app, config))
                return false;

            app.debug_object.mode_index = mode_index;

            std::ostringstream msg;
            msg
                << "debug scene mode selected index=" << mode_index
                << " name=" << config.name
                << " objects=" << config.object_count
                << " animation=" << debug_scene_animation_name(config.animation)
                << " culling_disabled=" << (config.culling_disabled ? "true" : "false");

            app.ctx.logger.info(msg.str());

            return true;
        }

        void handle_debug_scene_mode_input(
            wz::app::GameApp& app,
            const wz::input::InputState& input)
        {
            const auto& keyboard = input.keyboard;

            if (keyboard.pressed[VK_F1])
                set_debug_scene_mode(app, 0);
            else if (keyboard.pressed[VK_F2])
                set_debug_scene_mode(app, 1);
            else if (keyboard.pressed[VK_F3])
                set_debug_scene_mode(app, 2);
            else if (keyboard.pressed[VK_F4])
                set_debug_scene_mode(app, 3);
            else if (keyboard.pressed[VK_F5])
                set_debug_scene_mode(app, 4);
            else if (keyboard.pressed[VK_F6])
                set_debug_scene_mode(
                    app,
                    (app.debug_object.mode_index + 1u) %
                        static_cast<uint32_t>(kDebugSceneModes.size()));
        }

        wz::scene::ViewData build_view_data(
            const wz::app::RuntimeCamera& camera,
            const wz::engine::FrameContext& fctx)
        {
            using namespace wz::scene;
            using namespace wz::math;

            ViewData view{};
            view.camera_position = Vec3{ camera.x, camera.y, 0.0f };

            view.view = Mat4::identity();
            view.view.m[12] = -camera.x;
            view.view.m[13] = -camera.y;
            view.view.m[14] = 0.0f;

            constexpr float Pi = 3.14159265358979323846f;
            const float fov = 70.0f * Pi / 180.0f;

            const int win_w = fctx.input.window.width;
            const int win_h = fctx.input.window.height;

            const float aspect =
                (win_w > 0 && win_h > 0)
                ? static_cast<float>(win_w) / static_cast<float>(win_h)
                : 1280.0f / 720.0f;

            view.projection =
                wz::math::projection_perspective_dx(
                    fov,
                    aspect,
                    0.1f,
                    100.0f
                );

            view.view_projection = mul(view.projection, view.view);

            return view;
        }

        constexpr uint64_t kJobProfileLogEveryNFrames = 120;

        double ticks_to_ms(uint64_t ticks)
        {
            const double ticks_per_second =
                static_cast<double>(wz::time::TimeSource::ticks_per_second());

            return (static_cast<double>(ticks) * 1000.0) / ticks_per_second;
        }

        struct FrameAllocationSummary
        {
            uint64_t bytes_owned = 0;
            uint64_t bytes_allocated_this_frame = 0;
            uint32_t reallocations_this_frame = 0;
        };

        void reset_frame_allocation_counters(wz::app::GameApp& app)
        {
            app.frame.compiled_scene.stable_stats.reset_build_counters();
            app.frame.compiled_scene.view_stats.reset_build_counters();
            app.frame.compiled_scene.metadata_stats.reset_build_counters();
            app.frame.render_ir.stats.reset_build_counters();
            app.frame.render_frame.stable_stats.reset_build_counters();
            app.frame.render_frame.view_stats.reset_build_counters();
        }

        FrameAllocationSummary summarize_frame_allocations(
            const wz::app::GameApp& app)
        {
            FrameAllocationSummary out{};

            auto add = [&](const wz::scene_render::AllocationStats& s)
                {
                    out.bytes_owned += s.bytes_owned;
                    out.bytes_allocated_this_frame += s.bytes_allocated_this_build;
                    out.reallocations_this_frame += s.reallocations_this_build;
                };

            add(app.frame.compiled_scene.stable_stats);
            add(app.frame.compiled_scene.view_stats);
            add(app.frame.render_ir.stats);
            add(app.frame.render_frame.stable_stats);
            add(app.frame.render_frame.view_stats);

            return out;
        }

        void log_job_profile_if_due(
            wz::app::GameApp& app,
            const wz::jobs::FrameJobProfile& profile)
        {
            if (profile.timings.empty())
                return;

            if ((profile.frame_index % kJobProfileLogEveryNFrames) != 0)
                return;

            uint64_t total_ticks = 0;
            uint64_t render_prep_ticks = 0;
            const wz::jobs::JobTimingRecord* slowest = nullptr;

            for (const auto& rec : profile.timings)
            {
                const uint64_t dt = rec.duration_ticks();
                total_ticks += dt;

                const char* name = rec.name ? rec.name : "";

                if (
                    std::strcmp(name, "build_view") == 0 ||
                    std::strcmp(name, "compile_scene") == 0 ||
                    std::strcmp(name, "build_render_ir") == 0 ||
                    std::strcmp(name, "build_render_frame") == 0)
                {
                    render_prep_ticks += dt;
                }

                if (!slowest || dt > slowest->duration_ticks())
                    slowest = &rec;
            }

            const auto allocs = summarize_frame_allocations(app);

            const auto scene_nodes =
                app.debug_object.ready
                ? wz::core::graph::node_count(
                    app.debug_object.collision_scene.storage.polytree)
                : 0;
            const auto& culling = app.frame.render_ir.ir.culling;
            const DebugSceneConfig* config = app.debug_object.config;

            std::ostringstream summary;
            summary << std::fixed << std::setprecision(3);

            const auto cmd_count =
                app.frame.render_frame.frame.opaque.size()
                + app.frame.render_frame.frame.splats.size()
                + app.frame.render_frame.frame.transparent.size()
                + app.frame.render_frame.frame.particles.size();

            summary
                << "jobs frame=" << profile.frame_index
                << " mode=" << (config ? config->name : "<none>")
                << " path=" << wz::engine::render_prep_path_name(app.frame_dirty.render_prep_path())
                << " anim=" << (
                    config
                    ? debug_scene_animation_name(config->animation)
                    : "Unknown")
                << " renderables=" << app.debug_object.renderable_count
                << " culling_disabled=" << (
                    config && config->culling_disabled ? "true" : "false")
                << " dirty_nodes=" << app.debug_object.transform_affected_nodes.size()
                << " total=" << ticks_to_ms(total_ticks) << " ms"
                << " prep=" << ticks_to_ms(render_prep_ticks) << " ms"
                << " jobs=" << profile.timings.size()
                << " nodes=" << scene_nodes
                << " cmds=" << cmd_count
                << " visible_opaque=" << culling.visible_opaque
                << " culled_opaque=" << culling.culled_opaque
                << " allocs=" << allocs.reallocations_this_frame
                << " alloc_bytes=" << allocs.bytes_allocated_this_frame
                << " owned=" << allocs.bytes_owned;

            if (slowest)
            {
                summary
                    << " slowest="
                    << (slowest->name ? slowest->name : "<unnamed>")
                    << " "
                    << ticks_to_ms(slowest->duration_ticks())
                    << " ms";
            }

            app.ctx.logger.info(summary.str());

            for (const auto& rec : profile.timings)
            {
                std::ostringstream line;
                line << std::fixed << std::setprecision(3);

                line
                    << "  job "
                    << (rec.name ? rec.name : "<unnamed>")
                    << " "
                    << ticks_to_ms(rec.duration_ticks())
                    << " ms";

                app.ctx.logger.info(line.str());
            }
        }

        void job_build_view(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<BuildViewJobData*>(ctx.frame_user);
            assert(data);
            assert(data->fctx);
            assert(data->camera);
            assert(data->frame);

            data->frame->view =
                build_view_data(*data->camera, *data->fctx);
        }

        void job_compile_scene(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<RenderPrepJobData*>(ctx.frame_user);
            assert(data);
            assert(data->frame);
            assert(data->frame_dirty);
            assert(data->debug_object);

            auto& frame = *data->frame;
            auto& frame_dirty = *data->frame_dirty;
            auto& dbg = *data->debug_object;

            if (!dbg.ready)
                return;

            if (!dbg.compiled_scene_valid)
            {
                wz::scene::compile(
                    frame.compiled_scene,
                    dbg.collision_scene.storage.polytree,
                    dbg.descriptors,
                    {},
                    frame.view
                );

                dbg.compiled_scene_valid = true;
                dbg.transforms_dirty = false;

                frame_dirty.mark_render_full_compile();
                return;
            }

            // Debug animation currently moves only leaf renderable nodes.
            // If parent nodes become animated, replace this with dirty-root subtree
            // propagation and include all affected descendant renderable nodes.
            if (dbg.transforms_dirty)
            {
                update_world_for_nodes(
                    dbg.collision_scene.storage.polytree,
                    dbg.transform_affected_nodes
                );

                wz::scene::update_compiled_transforms(
                    frame.compiled_scene,
                    dbg.collision_scene.storage.polytree,
                    dbg.descriptors,
                    frame.view,
                    dbg.transform_affected_nodes,
                    true
                );

                dbg.transforms_dirty = false;

                frame_dirty.mark_render_transform_and_view();
                return;
            }

            wz::scene::update_view(
                frame.compiled_scene,
                frame.view
            );

            frame_dirty.mark_render_view_only();
        }

        void job_build_render_ir(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<RenderPrepJobData*>(ctx.frame_user);
            assert(data);
            assert(data->frame);
            assert(data->frame_dirty);
            assert(data->debug_object);

            if (!data->debug_object->ready)
                return;

            if (data->frame_dirty->render_prep_path() == RenderPrepPath::FullCompile)
            {
                wz::render::build_render_ir(
                    data->frame->render_ir,
                    data->frame->compiled_scene.scene
                );
            }
            else
            {
                wz::render::update_render_ir(data->frame->render_ir, data->frame->compiled_scene.scene);
            }
        }

        void job_build_collision_frame(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<CollisionFrameJobData*>(ctx.frame_user);
            assert(data);
            assert(data->frame);

            auto& collision = data->frame->collision;
            if (!data->scene || !data->collisions)
            {
                collision.world.clear();
                collision.current_pairs.clear();
                collision.prev_pairs.clear();
                collision.events.clear();
                collision.entity_events.clear();
                collision.routed_entity_events.clear();
                collision.proximity_world.clear();
                collision.proximity_current_pairs.clear();
                collision.proximity_prev_pairs.clear();
                collision.proximity_events.clear();
                collision.proximity_entity_events.clear();
                collision.routed_proximity_entity_events.clear();
                collision.proximity_pairs_tested = 0;
                collision.proximity_pairs_rejected = 0;
                collision.proximity_pairs_matched = 0;
                return;
            }

            wz::engine::collision::build_collision_frame(
                *data->scene,
                *data->collisions,
                collision);
            wz::engine::collision::build_proximity_frame(
                *data->scene,
                collision);
        }

        void job_build_input_events(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<InputEventJobData*>(ctx.frame_user);
            assert(data);
            assert(data->frame);

            auto& input_events = data->frame->input_events;
            if (!data->scene || !data->fctx)
            {
                input_events.events.clear();
                input_events.routed_entity_events.clear();
                return;
            }

            wz::engine::input_events::build_input_event_frame(
                data->fctx->input,
                *data->scene,
                input_events);
        }

        void job_dispatch_behaviors(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<BehaviorDispatchJobData*>(ctx.frame_user);
            assert(data);
            assert(data->frame);

            if (!data->scene || !data->registry || !data->fctx)
            {
                data->frame->behavior_commands.clear();
                return;
            }

            wz::engine::behavior::BehaviorFrameContext behavior_ctx{
                .frame_context = data->fctx,
                .frame_storage = data->frame,
                .scene = data->scene,
                .behavior_state = &data->scene->behavior_state,
                .commands = &data->frame->behavior_commands,
                .logger = data->logger,
            };
            wz::engine::behavior::dispatch_behaviors(
                *data->scene,
                *data->registry,
                behavior_ctx);
        }

        void job_apply_behavior_commands(wz::jobs::JobContext& ctx)
        {
            auto* data =
                static_cast<BehaviorCommandApplyJobData*>(ctx.frame_user);
            assert(data);
            assert(data->frame);

            if (!data->scene) {
                return;
            }

            std::vector<wz::scene::RuntimeEntityId> changed_entities;
            (void)wz::engine::behavior::apply_behavior_commands(
                    *data->scene,
                    data->frame->behavior_commands.commands,
                    &changed_entities);

            std::vector<wz::scene::RuntimeEntityId> velocity_changed;
            if (data->fctx) {
                (void)wz::engine::behavior::integrate_motion(
                    *data->scene,
                    static_cast<float>(data->fctx->frame.delta_seconds()),
                    &velocity_changed);
                changed_entities.insert(
                    changed_entities.end(),
                    velocity_changed.begin(),
                    velocity_changed.end());
                std::sort(changed_entities.begin(), changed_entities.end());
                changed_entities.erase(
                    std::unique(
                        changed_entities.begin(),
                        changed_entities.end()),
                    changed_entities.end());
            }

            if (changed_entities.empty()) {
                return;
            }

            if (data->debug_object) {
                // compile_scene has already consumed debug_object transform
                // dirtiness for this frame. Behavior commands patch the scene
                // graph and compiled transforms here, so do not carry a stale
                // debug animation dirty flag into the next frame.
                data->debug_object->transforms_dirty = false;
                if (data->debug_object->compiled_scene_valid) {
                    wz::scene::update_compiled_transforms(
                        data->frame->compiled_scene,
                        data->scene->storage.polytree,
                        data->debug_object->descriptors,
                        data->frame->view,
                        changed_entities,
                        true);
                }
            }

            if (data->frame_dirty
                && data->frame_dirty->render_prep_path()
                    != wz::engine::RenderPrepPath::FullCompile)
            {
                data->frame_dirty->mark_render_transform_and_view();
            }
        }

        void job_apply_terrain_constraints(wz::jobs::JobContext& ctx)
        {
            auto* data =
                static_cast<TerrainConstraintJobData*>(ctx.frame_user);
            assert(data);
            assert(data->frame);

            if (!data->scene) {
                return;
            }

            std::vector<wz::scene::RuntimeEntityId> changed_entities;
            (void)wz::engine::behavior::apply_terrain_constraints(
                *data->scene,
                data->frame->collision,
                &changed_entities);

            if (changed_entities.empty()) {
                return;
            }

            if (data->debug_object) {
                data->debug_object->transforms_dirty = false;
                if (data->debug_object->compiled_scene_valid) {
                    wz::scene::update_compiled_transforms(
                        data->frame->compiled_scene,
                        data->scene->storage.polytree,
                        data->debug_object->descriptors,
                        data->frame->view,
                        changed_entities,
                        true);
                }
            }

            if (data->frame_dirty
                && data->frame_dirty->render_prep_path()
                    != wz::engine::RenderPrepPath::FullCompile)
            {
                data->frame_dirty->mark_render_transform_and_view();
            }
        }

        void job_build_render_frame(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<RenderPrepJobData*>(ctx.frame_user);
            assert(data);
            assert(data->frame);
            assert(data->frame_dirty);
            assert(data->debug_object);

            if (!data->debug_object->ready)
                return;

            if (data->frame_dirty->render_prep_path() == RenderPrepPath::FullCompile)
            {
                wz::render::build_frame(
                    data->frame->render_frame,
                    data->frame->render_ir.ir,
                    data->frame->compiled_scene.scene
                );
            }
            else
            {
                wz::render::update_frame_view(
                    data->frame->render_frame,
                    data->frame->render_ir.ir,
                    data->frame->compiled_scene.scene
                );
            }
        }

        void job_platform_events(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<PlatformEventsJobData*>(ctx.frame_user);
            assert(data);
            assert(data->ctx);
            assert(data->app_ctx);

            wz::window::pump_messages();

            PlatformEvent event{};
            while (wz::window::poll_event(data->app_ctx->window, event))
            {
                switch (event.type)
                {
                case PlatformEvent::Type::Close:
                    data->ctx->running = false;
                    break;

                case PlatformEvent::Type::Resize:
                    if (event.resize.width > 0 && event.resize.height > 0)
                    {
                        wz::gpu::resize(
                            data->app_ctx->device,
                            event.resize.width,
                            event.resize.height
                        );
                    }
                    break;

                default:
                    break;
                }
            }

            if (wz::window::window_should_close(data->app_ctx->window))
                data->ctx->running = false;
        }

        void job_shutdown_input(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<ShutdownInputJobData*>(ctx.frame_user);
            assert(data);
            assert(data->ctx);
            assert(data->fctx);

            if (data->fctx->input.keyboard.pressed[VK_ESCAPE])
                data->ctx->running = false;
        }

        void job_update_camera(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<CameraUpdateJobData*>(ctx.frame_user);
            assert(data);
            assert(data->fctx);
            assert(data->camera);

            wz::app::update_camera(
                *data->camera,
                data->fctx->input,
                data->dt
            );
        }

        bool build_app_jobs(wz::app::GameApp& app)
        {
            auto& jobs = app.jobs;

            jobs.platform_events = jobs.graph.add_job({
                .name = "platform_events",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_platform_events,
                });

            jobs.shutdown_input = jobs.graph.add_job({
                .name = "shutdown_input",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_shutdown_input,
                });

            jobs.camera_update = jobs.graph.add_job({
                .name = "camera_update",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_update_camera,
                });

            jobs.build_view = jobs.graph.add_job({
                .name = "build_view",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_build_view,
                });

            jobs.compile_scene = jobs.graph.add_job({
                .name = "compile_scene",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_compile_scene,
                });

            jobs.build_collision_frame = jobs.graph.add_job({
                .name = "build_collision_frame",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_build_collision_frame,
                });

            jobs.build_input_events = jobs.graph.add_job({
                .name = "build_input_events",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_build_input_events,
                });

            jobs.dispatch_behaviors = jobs.graph.add_job({
                .name = "dispatch_behaviors",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_dispatch_behaviors,
                });

            jobs.apply_behavior_commands = jobs.graph.add_job({
                .name = "apply_behavior_commands",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_apply_behavior_commands,
                });

            jobs.apply_terrain_constraints = jobs.graph.add_job({
                .name = "apply_terrain_constraints",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_apply_terrain_constraints,
                });

            jobs.build_render_ir = jobs.graph.add_job({
                .name = "build_render_ir",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_build_render_ir,
                });

            jobs.build_render_frame = jobs.graph.add_job({
                .name = "build_render_frame",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_build_render_frame,
                });

            // Platform/window messages should run before input-derived app behavior.
            jobs.graph.add_dependency(jobs.platform_events, jobs.shutdown_input);
            jobs.graph.add_dependency(jobs.shutdown_input, jobs.camera_update);
            jobs.graph.add_dependency(jobs.camera_update, jobs.build_view);
            jobs.graph.add_dependency(jobs.build_view, jobs.compile_scene);
            jobs.graph.add_dependency(jobs.compile_scene, jobs.build_collision_frame);
            jobs.graph.add_dependency(jobs.build_collision_frame, jobs.build_input_events);
            jobs.graph.add_dependency(jobs.build_input_events, jobs.dispatch_behaviors);
            jobs.graph.add_dependency(jobs.dispatch_behaviors, jobs.apply_behavior_commands);
            jobs.graph.add_dependency(
                jobs.apply_behavior_commands,
                jobs.apply_terrain_constraints);
            jobs.graph.add_dependency(
                jobs.apply_terrain_constraints,
                jobs.build_render_ir);
            jobs.graph.add_dependency(jobs.build_render_ir, jobs.build_render_frame);
            jobs.ready = jobs.graph.commit();

            if (jobs.ready)
            {
                app.ctx.logger.info(
                    "app job graph committed: platform_events -> shutdown_input -> camera_update -> build_view -> compile_scene -> build_collision_frame -> build_input_events -> dispatch_behaviors -> apply_behavior_commands -> apply_terrain_constraints -> build_render_ir -> build_render_frame"
                );
            }
            else
            {
                app.ctx.logger.error("app update job graph commit failed");
            }

            return jobs.ready;
        }
    } // anonymous namespace

    void render_contents(
        GameApp& app,
        const wz::engine::FrameContext& fctx)
    {
        if (app.debug_object.ready)
        {
            wz::gpu::dx12::submit_render_frame(
                app.ctx.device,
                app.frame.render_frame.frame
            );
        }

        if (app.scalar_debug.ready)
        {
            wz::gpu::dx12::ScalarFieldDebugView view{};
            view.offset_x = app.camera.x * 0.10f;
            view.offset_y = app.camera.y * 0.10f;
            view.zoom = 1.0f;

            wz::gpu::dx12::submit_scalar_field_debug_frame(
                app.ctx.device,
                view
            );
        }
    }

    bool init(GameApp& app)
    {
        if (!wz::engine::init(app.ctx, {
                .window = {
                    .title  = "Wozzits Runtime",
                    .width  = 1280,
                    .height = 720,
                },
                .resource_root = "resources",
            }))
            return false;

        wz::engine::behavior::register_builtin_behaviors(
            app.behavior_registry,
            app.behavior_plugins,
            app.ctx.logger);

        using namespace wz::engine::assets;

        auto object_shaders = app.ctx.assets->shaders().create_shader_pair({
            .name         = "debug_opaque_object",
            .vertex_path  = "shaders/_debug/debug_object_vs.hlsl",
            .pixel_path   = "shaders/_debug/debug_object_ps.hlsl",
            });

        if (!object_shaders.valid())
            INIT_FAIL("create_shader_pair(debug_opaque_object)");

        if (!app.ctx.assets->commit())
            INIT_FAIL("assets commit");

        app.ctx.assets->resolve_all();

        auto object_shader_handles =
            app.ctx.assets->shaders().get_shader_pair(object_shaders);

        if (!object_shader_handles.valid())
            INIT_FAIL("get_shader_pair(debug_opaque_object)");

        wz::gpu::dx12::create_debug_opaque_context(app.ctx.device, {
            .vertex_shader = object_shader_handles.vertex,
            .pixel_shader  = object_shader_handles.pixel,
            });

        // Build the default runtime debug scene preset.
        if (!set_debug_scene_mode(app, kDefaultDebugSceneModeIndex))
            INIT_FAIL("build_debug_object_scene");

        // Scalar field debug is deliberately disabled for Session 7 object rendering.
        app.scalar_debug.texture = {};
        app.scalar_debug.ready   = false;

        wz::input::init_raw_input();

        if (!build_app_jobs(app))
            INIT_FAIL("build_app_jobs");

        return true;
    }

    void update(
        wz::engine::Context& ctx,
        wz::engine::FrameContext& fctx,
        GameApp& app)
    {
        assert(app.jobs.ready);

        handle_debug_scene_mode_input(app, fctx.input);

        app.debug_object.animation_time_seconds +=
            static_cast<float>(fctx.frame.delta_seconds());
        update_debug_object_animation(
            app,
            app.debug_object.animation_time_seconds);

        if (!app.jobs.update_logged)
        {
            app.ctx.logger.info("app update running through DagScheduler");
            app.jobs.update_logged = true;
        }

        PlatformEventsJobData platform_events_data{
            .ctx = &ctx,
            .app_ctx = &app.ctx,
        };

        ShutdownInputJobData shutdown_input_data{
            .ctx = &ctx,
            .fctx = &fctx,
        };

        CameraUpdateJobData camera_update_data{
            .camera = &app.camera,
            .fctx = &fctx,
            .dt = static_cast<float>(fctx.frame.delta_seconds()),
        };

        BuildViewJobData build_view_data{
            .frame = &app.frame,
            .camera = &app.camera,
            .fctx = &fctx,
        };

        RenderPrepJobData render_prep_data{
            .frame = &app.frame,
            .frame_dirty = &app.frame_dirty,
            .debug_object = &app.debug_object,
        };

        CollisionFrameJobData collision_frame_data{
            .frame = &app.frame,
            .scene =
                app.debug_object.collision_scene_valid
                ? &app.debug_object.collision_scene
                : nullptr,
            .collisions = app.ctx.assets ? &app.ctx.assets->collisions() : nullptr,
        };

        InputEventJobData input_event_data{
            .frame = &app.frame,
            .fctx = &fctx,
            .scene =
                app.debug_object.collision_scene_valid
                ? &app.debug_object.collision_scene
                : nullptr,
        };

        BehaviorDispatchJobData behavior_dispatch_data{
            .frame = &app.frame,
            .fctx = &fctx,
            .scene =
                app.debug_object.collision_scene_valid
                ? &app.debug_object.collision_scene
                : nullptr,
            .registry = &app.behavior_registry,
            .logger = &app.ctx.logger,
        };

        BehaviorCommandApplyJobData behavior_command_apply_data{
            .frame = &app.frame,
            .fctx = &fctx,
            .frame_dirty = &app.frame_dirty,
            .scene =
                app.debug_object.collision_scene_valid
                ? &app.debug_object.collision_scene
                : nullptr,
            .debug_object = &app.debug_object,
        };

        TerrainConstraintJobData terrain_constraint_data{
            .frame = &app.frame,
            .frame_dirty = &app.frame_dirty,
            .scene =
                app.debug_object.collision_scene_valid
                ? &app.debug_object.collision_scene
                : nullptr,
            .debug_object = &app.debug_object,
        };

        reset_frame_allocation_counters(app);

        app.jobs.exec.reset(app.jobs.graph);

        app.jobs.exec.bind(app.jobs.platform_events, &platform_events_data);
        app.jobs.exec.bind(app.jobs.shutdown_input, &shutdown_input_data);
        app.jobs.exec.bind(app.jobs.camera_update, &camera_update_data);

        app.jobs.exec.bind(app.jobs.build_view, &build_view_data);
        app.jobs.exec.bind(app.jobs.compile_scene, &render_prep_data);
        app.jobs.exec.bind(app.jobs.build_collision_frame, &collision_frame_data);
        app.jobs.exec.bind(app.jobs.build_input_events, &input_event_data);
        app.jobs.exec.bind(app.jobs.dispatch_behaviors, &behavior_dispatch_data);
        app.jobs.exec.bind(
            app.jobs.apply_behavior_commands,
            &behavior_command_apply_data);
        app.jobs.exec.bind(
            app.jobs.apply_terrain_constraints,
            &terrain_constraint_data);
        app.jobs.exec.bind(app.jobs.build_render_ir, &render_prep_data);
        app.jobs.exec.bind(app.jobs.build_render_frame, &render_prep_data);

        app.jobs.profile.reset(fctx.frame.index);
        app.jobs.scheduler.set_profile(&app.jobs.profile);
        app.jobs.scheduler.execute(app.jobs.graph, app.jobs.exec);
        app.jobs.scheduler.set_profile(nullptr);

        log_job_profile_if_due(app, app.jobs.profile);
    }

    void render(
        GameApp& app,
        const wz::engine::FrameContext& fctx)
    {
        wz::gpu::begin_frame(app.ctx.device);
        wz::gpu::clear(app.ctx.device, 0.0f, 0.15f, 0.35f, 1.0f);

        render_contents(app, fctx);

        wz::gpu::end_frame(app.ctx.device);
        wz::gpu::present(app.ctx.device);
    }

    GameAppBenchmarkSnapshot benchmark_snapshot(
        const GameApp& app,
        const wz::engine::FrameContext& fctx)
    {
        GameAppBenchmarkSnapshot out{};

        out.frame_index = fctx.frame.index;
        out.dt_seconds = fctx.frame.delta_seconds();

        if (out.dt_seconds > 0.0)
            out.fps = 1.0 / out.dt_seconds;

        out.debug_object_ready = app.debug_object.ready;
        out.compiled_scene_valid = app.debug_object.compiled_scene_valid;
        out.debug_renderables = app.debug_object.renderable_count;
        out.debug_scene_mode =
            app.debug_object.config ? app.debug_object.config->name : "";
        out.debug_animation =
            app.debug_object.config
            ? debug_scene_animation_name(app.debug_object.config->animation)
            : "Unknown";
        out.debug_culling_disabled =
            app.debug_object.config && app.debug_object.config->culling_disabled;

        out.dirty_nodes =
            static_cast<uint64_t>(
                app.debug_object.transform_affected_nodes.size());

        out.scene_nodes =
            app.debug_object.ready
            ? static_cast<uint64_t>(
                wz::core::graph::node_count(
                    app.debug_object.collision_scene.storage.polytree))
            : 0;

        out.opaque_commands =
            static_cast<uint64_t>(
                app.frame.render_frame.frame.opaque.size());

        out.splat_commands =
            static_cast<uint64_t>(
                app.frame.render_frame.frame.splats.size());

        out.transparent_commands =
            static_cast<uint64_t>(
                app.frame.render_frame.frame.transparent.size());

        out.particle_commands =
            static_cast<uint64_t>(
                app.frame.render_frame.frame.particles.size());

        out.total_commands =
            out.opaque_commands
            + out.splat_commands
            + out.transparent_commands
            + out.particle_commands;

        const auto allocs =
            summarize_frame_allocations(app);

        out.bytes_owned = allocs.bytes_owned;
        out.bytes_allocated_this_frame =
            allocs.bytes_allocated_this_frame;
        out.reallocations_this_frame =
            allocs.reallocations_this_frame;

        out.render_prep_path =
            wz::engine::render_prep_path_name(app.frame_dirty.render_prep_path());

        out.job_count = 0;
        out.total_job_ms = 0.0;
        out.render_prep_job_ms = 0.0;
        out.slowest_job_name = "";
        out.slowest_job_ms = 0.0;

        for (const auto& rec : app.jobs.profile.timings)
        {
            const double ms = ticks_to_ms(rec.duration_ticks());

            out.total_job_ms += ms;

            const char* name = rec.name ? rec.name : "<unnamed>";

            if (
                std::strcmp(name, "build_view") == 0 ||
                std::strcmp(name, "compile_scene") == 0 ||
                std::strcmp(name, "build_render_ir") == 0 ||
                std::strcmp(name, "build_render_frame") == 0)
            {
                out.render_prep_job_ms += ms;
            }

            if (ms > out.slowest_job_ms)
            {
                out.slowest_job_ms = ms;
                out.slowest_job_name = name;
            }

            if (out.job_count < kGameAppBenchmarkMaxJobs)
            {
                out.jobs[out.job_count++] = GameAppJobTimingSnapshot{
                    .name = name,
                    .ms = ms,
                };
            }
        }


        return out;
    }

    void shutdown(GameApp& app)
    {
        wz::engine::shutdown(app.ctx);
    }
}
