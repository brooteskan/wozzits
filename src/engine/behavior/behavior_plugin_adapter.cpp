#include <engine/behavior/behavior_plugin_adapter.h>

#include <engine/assets/scene/scene_instance.h>
#include <engine/collision/collision_frame.h>
#include <engine/frame_storage.h>

#include <input/input.h>
#include <logging/logger.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <scene/scene_graph.h>

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
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
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
            case Status::CopyFailed:
                return "copy_failed";
            case Status::MissingRegisterSymbol:
                return "missing_register_symbol";
            case Status::RegistrationFailed:
                return "registration_failed";
            case Status::UnsupportedPlatform:
                return "unsupported_platform";
            }
            return "unknown";
        }

#if defined(_WIN32)
        std::filesystem::path make_behavior_load_copy_path(
            const std::filesystem::path& path)
        {
            static std::atomic<uint64_t> counter{ 0 };

            const uint64_t id = counter.fetch_add(1u);
            const std::wstring suffix =
                L".wzload." + std::to_wstring(GetCurrentProcessId()) + L"."
                + std::to_wstring(id);
            return path.parent_path()
                / (path.stem().wstring() + suffix
                    + path.extension().wstring());
        }

        bool is_behavior_load_copy_path(
            const std::filesystem::path& path)
        {
            return path.filename().wstring().find(L".wzload.")
                != std::wstring::npos;
        }

        void remove_behavior_load_copy(const std::string& path)
        {
            if (path.empty()) {
                return;
            }

            std::error_code ec;
            std::filesystem::remove(std::filesystem::path{ path }, ec);
        }
#endif

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
            case WZ_BEHAVIOR_COMMAND_ADD_LOCAL_SCALE:
                return BehaviorCommandKind::AddLocalScale;
            case WZ_BEHAVIOR_COMMAND_SET_LOCAL_SCALE:
                return BehaviorCommandKind::SetLocalScale;
            case WZ_BEHAVIOR_COMMAND_SET_LOCAL_ROTATION:
                return BehaviorCommandKind::SetLocalRotation;
            case WZ_BEHAVIOR_COMMAND_ADD_WORLD_TRANSLATION:
                return BehaviorCommandKind::AddWorldTranslation;
            case WZ_BEHAVIOR_COMMAND_SET_WORLD_TRANSLATION:
                return BehaviorCommandKind::SetWorldTranslation;
            case WZ_BEHAVIOR_COMMAND_SET_LINEAR_VELOCITY:
                return BehaviorCommandKind::SetLinearVelocity;
            case WZ_BEHAVIOR_COMMAND_SET_ANGULAR_VELOCITY:
                return BehaviorCommandKind::SetAngularVelocity;
            case WZ_BEHAVIOR_COMMAND_SET_MOTION_SPACE:
                return BehaviorCommandKind::SetMotionSpace;
            case WZ_BEHAVIOR_COMMAND_NONE:
            default:
                return BehaviorCommandKind::None;
            }
        }

        bool entity_valid(
            const wz::engine::assets::SceneInstance* scene,
            wz::scene::RuntimeEntityId entity) noexcept
        {
            return scene
                && entity != wz::scene::INVALID_RUNTIME_ENTITY
                && entity < wz::core::graph::node_count(
                    scene->storage.polytree);
        }

        void fill_mat4(
            const wz::math::Mat4& matrix,
            WzMat4& out) noexcept
        {
            for (uint32_t i = 0; i < 16; ++i) {
                out.m[i] = matrix.m[i];
            }
        }

        bool normalize_checked(wz::math::Vec3& v) noexcept
        {
            const float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
            if (len_sq <= 1e-12f || !std::isfinite(len_sq)) {
                return false;
            }
            v = wz::math::normalize(v);
            return true;
        }

        bool ray_triangle_hit(
            const wz::math::Vec3& origin,
            const wz::math::Vec3& direction,
            const wz::math::Vec3& a,
            const wz::math::Vec3& b,
            const wz::math::Vec3& c,
            float max_distance,
            float& out_distance,
            wz::math::Vec3& out_position,
            wz::math::Vec3& out_normal) noexcept
        {
            const wz::math::Vec3 edge1 = b - a;
            const wz::math::Vec3 edge2 = c - a;
            const wz::math::Vec3 pvec = wz::math::cross(direction, edge2);
            const float det = wz::math::dot(edge1, pvec);
            if (std::abs(det) <= 1e-8f) {
                return false;
            }

            const float inv_det = 1.0f / det;
            const wz::math::Vec3 tvec = origin - a;
            const float u = wz::math::dot(tvec, pvec) * inv_det;
            if (u < -1e-5f || u > 1.0f + 1e-5f) {
                return false;
            }

            const wz::math::Vec3 qvec = wz::math::cross(tvec, edge1);
            const float v = wz::math::dot(direction, qvec) * inv_det;
            if (v < -1e-5f || u + v > 1.0f + 1e-5f) {
                return false;
            }

            const float t = wz::math::dot(edge2, qvec) * inv_det;
            if (t < 0.0f || t > max_distance) {
                return false;
            }

            wz::math::Vec3 normal = wz::math::cross(edge1, edge2);
            if (!normalize_checked(normal)) {
                return false;
            }
            if (wz::math::dot(normal, direction) >= 0.0f) {
                normal.x = -normal.x;
                normal.y = -normal.y;
                normal.z = -normal.z;
            }

            out_distance = t;
            out_position = origin + direction * t;
            out_normal = normal;
            return true;
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

            out.controller_count = input.controllers.count;
            for (uint32_t controller_index = 0;
                 controller_index < input.controllers.count
                     && controller_index < wz::input::kMaxControllers;
                 ++controller_index)
            {
                const auto& controller =
                    input.controllers.controllers[controller_index];
                out.controller_connected[controller_index] =
                    controller.connected ? uint8_t{ 1 } : uint8_t{ 0 };
                out.controller_connected_pressed[controller_index] =
                    controller.connected_pressed ? uint8_t{ 1 } : uint8_t{ 0 };
                out.controller_connected_released[controller_index] =
                    controller.connected_released ? uint8_t{ 1 } : uint8_t{ 0 };

                for (uint32_t axis = 0;
                     axis < wz::input::kControllerAxisCount;
                     ++axis)
                {
                    out.controller_axes[controller_index][axis] =
                        controller.axes[axis];
                }
                for (uint32_t button = 0;
                     button < wz::input::kControllerButtonCount;
                     ++button)
                {
                    out.controller_buttons[controller_index][button] =
                        controller.buttons[button]
                            ? uint8_t{ 1 }
                            : uint8_t{ 0 };
                    out.controller_buttons_pressed[controller_index][button] =
                        controller.buttons_pressed[button]
                            ? uint8_t{ 1 }
                            : uint8_t{ 0 };
                    out.controller_buttons_released[controller_index][button] =
                        controller.buttons_released[button]
                            ? uint8_t{ 1 }
                            : uint8_t{ 0 };
                }
            }
        }

        void fill_timing_view(
            const wz::engine::FrameContext& frame,
            WzFrameTiming& out) noexcept
        {
            out.delta_seconds =
                static_cast<float>(frame.frame.delta_seconds());
            out.elapsed_seconds =
                static_cast<double>(frame.frame.interval.end)
                / static_cast<double>(wz::time::TimeSource::ticks_per_second());
            out.frame_index = frame.frame.index;
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

        uint8_t get_local_transform(
            void* user,
            WzBehaviorEntityId entity,
            WzMat4* out_transform)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !out_transform
                || !entity_valid(context->scene, entity))
            {
                return 0;
            }

            const auto& node = wz::core::graph::node_data(
                context->scene->storage.polytree,
                entity);
            fill_mat4(node.local, *out_transform);
            return 1;
        }

        uint8_t get_world_transform(
            void* user,
            WzBehaviorEntityId entity,
            WzMat4* out_transform)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !out_transform
                || !entity_valid(context->scene, entity))
            {
                return 0;
            }

            const auto& node = wz::core::graph::node_data(
                context->scene->storage.polytree,
                entity);
            fill_mat4(node.world, *out_transform);
            return 1;
        }

        uint8_t get_local_position(
            void* user,
            WzBehaviorEntityId entity,
            WzVec3* out_position)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !out_position
                || !entity_valid(context->scene, entity))
            {
                return 0;
            }

            const auto& node = wz::core::graph::node_data(
                context->scene->storage.polytree,
                entity);
            *out_position = WzVec3{
                .x = node.local.m[12],
                .y = node.local.m[13],
                .z = node.local.m[14],
            };
            return 1;
        }

        uint8_t get_world_position(
            void* user,
            WzBehaviorEntityId entity,
            WzVec3* out_position)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !out_position
                || !entity_valid(context->scene, entity))
            {
                return 0;
            }

            const auto& node = wz::core::graph::node_data(
                context->scene->storage.polytree,
                entity);
            *out_position = WzVec3{
                .x = node.world.m[12],
                .y = node.world.m[13],
                .z = node.world.m[14],
            };
            return 1;
        }

        uint8_t query_collision_surface_ray(
            void* user,
            WzBehaviorEntityId surface_entity,
            WzVec3 origin,
            WzVec3 direction,
            float max_distance,
            WzSurfaceSample* out_sample)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->frame_storage || !out_sample
                || max_distance <= 0.0f
                || !std::isfinite(max_distance))
            {
                return 0;
            }

            *out_sample = WzSurfaceSample{
                .hit = 0u,
                .surface_entity =
                    static_cast<WzBehaviorEntityId>(
                        WZ_INVALID_BEHAVIOR_ENTITY),
                .position = WzVec3{},
                .normal = WzVec3{ .x = 0.0f, .y = 1.0f, .z = 0.0f },
            };

            wz::math::Vec3 ray_origin{
                .x = origin.x,
                .y = origin.y,
                .z = origin.z,
            };
            wz::math::Vec3 ray_direction{
                .x = direction.x,
                .y = direction.y,
                .z = direction.z,
            };
            if (!normalize_checked(ray_direction)) {
                return 0;
            }

            float best_distance = std::numeric_limits<float>::max();
            wz::math::Vec3 best_position{};
            wz::math::Vec3 best_normal{ .x = 0.0f, .y = 1.0f, .z = 0.0f };
            wz::scene::RuntimeEntityId best_surface =
                wz::scene::INVALID_RUNTIME_ENTITY;

            for (const auto& entry : context->frame_storage->collision.world) {
                if (entry.entity != surface_entity
                    || !entry.enabled
                    || !entry.resolved
                    || !entry.resolved->occupancy.queryable
                    || entry.resolved->shape_kind
                        != wz::engine::assets::CollisionShapeKind::
                            TerrainMeshSurface)
                {
                    continue;
                }

                const auto& data = *entry.resolved;
                if (data.points.empty() || data.indices.size() < 3u) {
                    continue;
                }

                const auto make_local_point =
                    [](const wz::engine::assets::CollisionPoint& point)
                {
                    return wz::math::Vec3{
                        .x = point.position[0],
                        .y = point.position[1],
                        .z = point.position[2],
                    };
                };
                const uint32_t triangle_count =
                    static_cast<uint32_t>(data.indices.size() / 3u);
                for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                    const size_t index = static_cast<size_t>(tri) * 3u;
                    if (index + 2u >= data.indices.size()) {
                        continue;
                    }

                    const uint32_t ia = data.indices[index + 0u];
                    const uint32_t ib = data.indices[index + 1u];
                    const uint32_t ic = data.indices[index + 2u];
                    if (ia >= data.points.size()
                        || ib >= data.points.size()
                        || ic >= data.points.size())
                    {
                        continue;
                    }

                    const wz::math::Vec3 a = wz::math::mul_point(
                        entry.world_from_local,
                        make_local_point(data.points[ia]));
                    const wz::math::Vec3 b = wz::math::mul_point(
                        entry.world_from_local,
                        make_local_point(data.points[ib]));
                    const wz::math::Vec3 c = wz::math::mul_point(
                        entry.world_from_local,
                        make_local_point(data.points[ic]));

                    float distance = 0.0f;
                    wz::math::Vec3 position{};
                    wz::math::Vec3 normal{};
                    if (ray_triangle_hit(
                            ray_origin,
                            ray_direction,
                            a,
                            b,
                            c,
                            max_distance,
                            distance,
                            position,
                            normal)
                        && distance < best_distance)
                    {
                        best_distance = distance;
                        best_position = position;
                        best_normal = normal;
                        best_surface = entry.entity;
                    }
                }
            }

            if (best_surface == wz::scene::INVALID_RUNTIME_ENTITY) {
                return 0;
            }

            *out_sample = WzSurfaceSample{
                .hit = 1u,
                .surface_entity = best_surface,
                .position = WzVec3{
                    .x = best_position.x,
                    .y = best_position.y,
                    .z = best_position.z,
                },
                .normal = WzVec3{
                    .x = best_normal.x,
                    .y = best_normal.y,
                    .z = best_normal.z,
                },
            };
            return 1;
        }

        uint8_t find_entity_by_authored_id(
            void* user,
            const char* authored_id,
            WzBehaviorEntityId* out_entity)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->scene || !authored_id || !out_entity) {
                return 0;
            }

            const auto it =
                context->scene->authored_to_runtime.find(authored_id);
            if (it == context->scene->authored_to_runtime.end()) {
                return 0;
            }

            *out_entity = it->second;
            return 1;
        }

        uint8_t find_entity_by_name(
            void* user,
            const char* name,
            WzBehaviorEntityId* out_entity)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->scene || !name || !out_entity) {
                return 0;
            }

            const auto& names = context->scene->runtime_names;
            for (uint32_t i = 0; i < names.size(); ++i) {
                if (names[i] == name) {
                    *out_entity = i;
                    return 1;
                }
            }
            return 0;
        }

        const wz::engine::assets::SceneBehaviorConfigValue*
        find_config_value(BehaviorFrameContext* context, const char* key)
        {
            if (!context || !context->active_behavior || !key) {
                return nullptr;
            }

            for (const auto& entry : context->active_behavior->config) {
                if (entry.key == key) {
                    return &entry;
                }
            }
            return nullptr;
        }

        uint8_t get_config_bool(
            void* user,
            const char* key,
            uint8_t* out_value)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            const auto* entry = find_config_value(context, key);
            if (!entry || !out_value
                || entry->kind
                    != wz::engine::assets::SceneBehaviorConfigValueKind::Bool)
            {
                return 0;
            }

            *out_value = entry->bool_value ? uint8_t{ 1 } : uint8_t{ 0 };
            return 1;
        }

        uint8_t get_config_number(
            void* user,
            const char* key,
            double* out_value)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            const auto* entry = find_config_value(context, key);
            if (!entry || !out_value
                || entry->kind
                    != wz::engine::assets::SceneBehaviorConfigValueKind::Number)
            {
                return 0;
            }

            *out_value = entry->number_value;
            return 1;
        }

        uint8_t get_config_string(
            void* user,
            const char* key,
            char* out_buffer,
            uint32_t buffer_size,
            uint32_t* out_required_size)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            const auto* entry = find_config_value(context, key);
            if (!entry
                || entry->kind
                    != wz::engine::assets::SceneBehaviorConfigValueKind::String)
            {
                return 0;
            }

            const uint32_t required_size =
                static_cast<uint32_t>(entry->string_value.size() + 1u);
            if (out_required_size) {
                *out_required_size = required_size;
            }

            if (!out_buffer || buffer_size == 0u) {
                return 1;
            }

            const uint32_t copy_size =
                std::min(buffer_size - 1u, required_size - 1u);
            if (copy_size > 0u) {
                std::memcpy(
                    out_buffer,
                    entry->string_value.data(),
                    copy_size);
            }
            out_buffer[copy_size] = '\0';
            return required_size <= buffer_size ? uint8_t{ 1 } : uint8_t{ 0 };
        }

        void* get_instance_state(void* user)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->behavior_state
                || !context->active_behavior
                || context->active_behavior->binding_id.empty())
            {
                return nullptr;
            }

            auto* block = context->behavior_state->find_instance_state(
                context->active_behavior->binding_id);
            return block ? block->data : nullptr;
        }

        void* alloc_instance_state(
            void* user,
            uint32_t size,
            uint32_t alignment)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->behavior_state
                || !context->active_behavior
                || context->active_behavior->binding_id.empty())
            {
                return nullptr;
            }

            auto* block = context->behavior_state->allocate_instance_state(
                context->active_behavior->binding_id,
                size,
                alignment);
            return block ? block->data : nullptr;
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
            WzFrameTiming timing_view{};
            const WzFrameTiming* timing = nullptr;
            if (context.frame_context) {
                fill_input_view(context.frame_context->input, input_view);
                input = &input_view;
                fill_timing_view(*context.frame_context, timing_view);
                timing = &timing_view;
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
                .transform_query_user = &context,
                .get_local_transform = get_local_transform,
                .get_world_transform = get_world_transform,
                .get_local_position = get_local_position,
                .get_world_position = get_world_position,
                .command_writer_user = &context,
                .write_command = write_behavior_command,
                .log_user = binding->logger,
                .log_info = log_info,
                .collision_query_user = &context,
                .query_collision_surface_ray = query_collision_surface_ray,
                .timing = timing,
                .scene_query_user = &context,
                .find_entity_by_name = find_entity_by_name,
                .find_entity_by_authored_id = find_entity_by_authored_id,
                .behavior_config_user = &context,
                .get_config_bool = get_config_bool,
                .get_config_number = get_config_number,
                .get_config_string = get_config_string,
                .active_input_event = context.active_input_payload,
                .behavior_state_user = &context,
                .get_instance_state = get_instance_state,
            };

            binding->function(&facts, entity, binding->user_data);
        }

        WzBehaviorFrameFacts make_frame_facts(
            BehaviorFrameContext& context,
            wz::Logger* logger,
            WzInputStateView& input_view,
            WzFrameTiming& timing_view)
        {
            const WzInputStateView* input = nullptr;
            const WzFrameTiming* timing = nullptr;
            if (context.frame_context) {
                fill_input_view(context.frame_context->input, input_view);
                input = &input_view;
                fill_timing_view(*context.frame_context, timing_view);
                timing = &timing_view;
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
                .transform_query_user = &context,
                .get_local_transform = get_local_transform,
                .get_world_transform = get_world_transform,
                .get_local_position = get_local_position,
                .get_world_position = get_world_position,
                .command_writer_user = &context,
                .write_command = write_behavior_command,
                .log_user = logger,
                .log_info = log_info,
                .collision_query_user = &context,
                .query_collision_surface_ray = query_collision_surface_ray,
                .timing = timing,
                .scene_query_user = &context,
                .find_entity_by_name = find_entity_by_name,
                .find_entity_by_authored_id = find_entity_by_authored_id,
                .behavior_config_user = &context,
                .get_config_bool = get_config_bool,
                .get_config_number = get_config_number,
                .get_config_string = get_config_string,
                .active_input_event = context.active_input_payload,
                .behavior_state_user = &context,
                .get_instance_state = get_instance_state,
            };
        }

        WzBehaviorInitFacts make_init_facts(
            BehaviorFrameContext& context,
            wz::Logger* logger)
        {
            return WzBehaviorInitFacts{
                .transform_query_user = &context,
                .get_local_transform = get_local_transform,
                .get_world_transform = get_world_transform,
                .get_local_position = get_local_position,
                .get_world_position = get_world_position,
                .log_user = logger,
                .log_info = log_info,
                .scene_query_user = &context,
                .find_entity_by_name = find_entity_by_name,
                .find_entity_by_authored_id = find_entity_by_authored_id,
                .behavior_config_user = &context,
                .get_config_bool = get_config_bool,
                .get_config_number = get_config_number,
                .get_config_string = get_config_string,
                .behavior_state_user = &context,
                .alloc_instance_state = alloc_instance_state,
                .get_instance_state = get_instance_state,
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
            WzFrameTiming timing_view{};
            WzBehaviorFrameFacts facts =
                make_frame_facts(
                    context,
                    binding->logger,
                    input_view,
                    timing_view);
            const WzBehaviorEvent abi_event{
                .kind = to_abi_behavior_event_kind(event),
                .entity = event.entity,
                .other = event.other,
                .self_is_trigger =
                    event.self_is_trigger ? uint8_t{ 1 } : uint8_t{ 0 },
            };

            binding->on_event(&facts, &abi_event, binding->user_data);
        }

        void dispatch_abi_module_init(
            BehaviorFrameContext& context,
            wz::scene::RuntimeEntityId entity,
            void* user_data)
        {
            auto* binding =
                static_cast<BehaviorPluginHost::Binding*>(user_data);
            if (!binding || !binding->on_init) {
                return;
            }

            WzBehaviorInitFacts facts =
                make_init_facts(context, binding->logger);
            binding->on_init(&facts, entity, binding->user_data);
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
                nullptr,
                module_user_data,
                context->logger);
            const BehaviorModuleHandle handle =
                context->registry->register_module(
                    module,
                    dispatch_abi_module_event,
                    binding_ptr);
            return handle.valid() ? uint8_t{ 1 } : uint8_t{ 0 };
        }

        uint8_t register_module_desc(
            void* user,
            const WzBehaviorModuleDesc* desc)
        {
            const uint32_t required_size =
                static_cast<uint32_t>(
                    offsetof(WzBehaviorModuleDesc, on_init));
            const bool has_on_init =
                desc
                && desc->size
                    >= offsetof(WzBehaviorModuleDesc, event_channels);
            const bool has_event_channels =
                desc
                && desc->size
                    >= offsetof(WzBehaviorModuleDesc, event_channels)
                        + sizeof(desc->event_channels);
            const bool has_event_channel_count =
                desc
                && desc->size
                    >= offsetof(WzBehaviorModuleDesc, event_channel_count)
                        + sizeof(desc->event_channel_count);
            const bool has_module_user_data =
                desc
                && desc->size
                    >= offsetof(WzBehaviorModuleDesc, module_user_data)
                        + sizeof(desc->module_user_data);
            auto* context = static_cast<RegisterContext*>(user);
            const WzBehaviorInitFn on_init =
                has_on_init ? desc->on_init : nullptr;
            if (!context || !context->registry || !context->host
                || !desc || desc->size < required_size
                || !desc->module || (!desc->on_event && !on_init))
            {
                return 0;
            }

            std::vector<std::string> default_events;
            const uint32_t event_channel_count =
                has_event_channel_count ? desc->event_channel_count : 0u;
            default_events.reserve(event_channel_count);
            for (uint32_t i = 0; i < event_channel_count; ++i) {
                if (!has_event_channels
                    || !desc->event_channels
                    || !desc->event_channels[i]
                    || desc->event_channels[i][0] == '\0')
                {
                    continue;
                }
                default_events.emplace_back(desc->event_channels[i]);
            }

            const auto compiled = compile_channel_mask(default_events);
            if (context->logger && compiled.unknown_count > 0u) {
                context->logger->warn(
                    std::string("behavior module '")
                    + desc->module
                    + "' default events have "
                    + std::to_string(compiled.unknown_count)
                    + " unknown channel token(s)");
            }
            if (context->logger && compiled.redundant_count > 0u) {
                context->logger->warn(
                    std::string("behavior module '")
                    + desc->module
                    + "' default events have "
                    + std::to_string(compiled.redundant_count)
                    + " redundant channel token(s)");
            }
            auto* binding_ptr = context->host->add_module_binding(
                desc->on_event,
                on_init,
                has_module_user_data ? desc->module_user_data : nullptr,
                context->logger);
            const BehaviorModuleHandle handle =
                context->registry->register_module(
                    desc->module,
                    desc->on_event ? dispatch_abi_module_event : nullptr,
                    on_init ? dispatch_abi_module_init : nullptr,
                    std::move(default_events),
                    compiled.mask,
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
            .register_module_desc = register_module_desc,
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
        const std::filesystem::path loaded_path =
            make_behavior_load_copy_path(path);
        std::error_code copy_ec;
        std::filesystem::copy_file(
            path,
            loaded_path,
            std::filesystem::copy_options::overwrite_existing,
            copy_ec);
        if (copy_ec) {
            return {
                .status = DynamicLoadStatus::CopyFailed,
                .detail = path.string() + " -> " + loaded_path.string()
                    + " error=" + copy_ec.message(),
            };
        }

        HMODULE module = LoadLibraryW(loaded_path.wstring().c_str());
        if (!module) {
            const DWORD error = GetLastError();
            remove_behavior_load_copy(loaded_path.string());
            return {
                .status = DynamicLoadStatus::LoadFailed,
                .detail =
                    loaded_path.string() + " error=" + std::to_string(error),
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
            remove_behavior_load_copy(loaded_path.string());
            return {
                .status = DynamicLoadStatus::MissingRegisterSymbol,
                .detail = symbol,
            };
        }

        if (!register_static_pack(registry, register_plugin, logger)) {
            FreeLibrary(module);
            remove_behavior_load_copy(loaded_path.string());
            return {
                .status = DynamicLoadStatus::RegistrationFailed,
                .detail = path.string(),
            };
        }

        dynamic_modules_.push_back(DynamicModule{
            .handle = module,
            .path = path.string(),
            .loaded_path = loaded_path.string(),
        });
        return {
            .status = DynamicLoadStatus::Loaded,
            .detail = path.string() + " -> " + loaded_path.string(),
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
            if (is_behavior_load_copy_path(path)) {
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
            remove_behavior_load_copy(module.loaded_path);
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
        WzBehaviorInitFn on_init,
        void* user_data,
        wz::Logger* logger)
    {
        auto binding = std::make_unique<Binding>(Binding{
            .on_event = on_event,
            .on_init = on_init,
            .user_data = user_data,
            .logger = logger,
        });
        Binding* out = binding.get();
        bindings_.push_back(std::move(binding));
        return out;
    }
}
