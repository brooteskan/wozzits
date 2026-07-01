#include <engine/behavior/behavior_plugin_adapter.h>

#include <engine/behavior/quantum_agent_behaviors.h>

#include <engine/assets/scene/scene_instance.h>
#include <engine/collision/collision_frame.h>
#include <engine/collision/collision_surface_sampling.h>
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

        bool same_dynamic_module_path(
            const BehaviorPluginHost::DynamicModule& module,
            const std::filesystem::path& path)
        {
            return std::filesystem::path{ module.path } == path;
        }

        void unload_dynamic_module_copy(
            BehaviorPluginHost::DynamicModule& module)
        {
            if (module.handle) {
                FreeLibrary(static_cast<HMODULE>(module.handle));
                module.handle = nullptr;
            }
            remove_behavior_load_copy(module.loaded_path);
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
            case WZ_BEHAVIOR_COMMAND_SET_ACTIVE_CAMERA:
                return BehaviorCommandKind::SetActiveCamera;
            case WZ_BEHAVIOR_COMMAND_PLAY_SOUND:
                return BehaviorCommandKind::PlaySound;
            case WZ_BEHAVIOR_COMMAND_STOP_SOUND:
                return BehaviorCommandKind::StopSound;
            case WZ_BEHAVIOR_COMMAND_SET_SOUND_GAIN:
                return BehaviorCommandKind::SetSoundGain;
            case WZ_BEHAVIOR_COMMAND_SET_GRAIN_PARAM:
                return BehaviorCommandKind::SetGrainParam;
            case WZ_BEHAVIOR_COMMAND_SPAWN_PREFAB:
                return BehaviorCommandKind::SpawnPrefab;
            case WZ_BEHAVIOR_COMMAND_SET_TERRAIN_ALIGNMENT_RATE:
                return BehaviorCommandKind::SetTerrainAlignmentRate;
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

        bool inverse_affine_point(
            const wz::math::Mat4& m,
            const wz::math::Vec3& p,
            wz::math::Vec3& out) noexcept
        {
            const float a00 = m.m[0];
            const float a01 = m.m[4];
            const float a02 = m.m[8];
            const float a10 = m.m[1];
            const float a11 = m.m[5];
            const float a12 = m.m[9];
            const float a20 = m.m[2];
            const float a21 = m.m[6];
            const float a22 = m.m[10];

            const float det =
                a00 * (a11 * a22 - a12 * a21)
                - a01 * (a10 * a22 - a12 * a20)
                + a02 * (a10 * a21 - a11 * a20);
            if (std::abs(det) <= 1e-8f) {
                return false;
            }

            const float inv_det = 1.0f / det;
            const float x = p.x - m.m[12];
            const float y = p.y - m.m[13];
            const float z = p.z - m.m[14];

            out.x =
                ((a11 * a22 - a12 * a21) * x
                    + (a02 * a21 - a01 * a22) * y
                    + (a01 * a12 - a02 * a11) * z)
                * inv_det;
            out.y =
                ((a12 * a20 - a10 * a22) * x
                    + (a00 * a22 - a02 * a20) * y
                    + (a02 * a10 - a00 * a12) * z)
                * inv_det;
            out.z =
                ((a10 * a21 - a11 * a20) * x
                    + (a01 * a20 - a00 * a21) * y
                    + (a00 * a11 - a01 * a10) * z)
                * inv_det;
            return true;
        }

        bool ray_axis_clip(
            float origin,
            float direction,
            float min_value,
            float max_value,
            float& t_min,
            float& t_max) noexcept
        {
            constexpr float k_epsilon = 1e-8f;
            if (std::abs(direction) <= k_epsilon) {
                return origin >= min_value && origin <= max_value;
            }

            float t0 = (min_value - origin) / direction;
            float t1 = (max_value - origin) / direction;
            if (t0 > t1) {
                std::swap(t0, t1);
            }

            t_min = (std::max)(t_min, t0);
            t_max = (std::min)(t_max, t1);
            return t_min <= t_max;
        }

        bool ray_bounds_hit(
            const wz::engine::assets::CollisionTriangleBounds& bounds,
            const wz::math::Vec3& origin,
            const wz::math::Vec3& direction,
            float max_distance) noexcept
        {
            float t_min = 0.0f;
            float t_max = max_distance;
            return ray_axis_clip(
                    origin.x,
                    direction.x,
                    bounds.min[0],
                    bounds.max[0],
                    t_min,
                    t_max)
                && ray_axis_clip(
                    origin.y,
                    direction.y,
                    bounds.min[1],
                    bounds.max[1],
                    t_min,
                    t_max)
                && ray_axis_clip(
                    origin.z,
                    direction.z,
                    bounds.min[2],
                    bounds.max[2],
                    t_min,
                    t_max);
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

        enum class SurfaceRayGridResult
        {
            Unavailable,
            Miss,
            Hit,
        };

        uint32_t surface_grid_cell_index(
            float value,
            float origin,
            float size,
            uint32_t count) noexcept
        {
            const float normalized = (value - origin) / size;
            const int raw = static_cast<int>(std::floor(normalized));
            return static_cast<uint32_t>(
                (std::clamp)(raw, 0, static_cast<int>(count) - 1));
        }

        wz::math::Vec3 collision_point_position(
            const wz::engine::assets::CollisionPoint& point) noexcept
        {
            return wz::math::Vec3{
                .x = point.position[0],
                .y = point.position[1],
                .z = point.position[2],
            };
        }

        SurfaceRayGridResult query_grid_surface_ray(
            const wz::engine::collision::CollisionWorldEntry& entry,
            const wz::math::Vec3& ray_origin,
            const wz::math::Vec3& ray_direction,
            float max_distance,
            float& best_distance,
            wz::math::Vec3& best_position,
            wz::math::Vec3& best_normal,
            BehaviorSurfaceRayQueryStats* stats) noexcept
        {
            const auto& data = *entry.resolved;
            const auto& grid = data.surface_grid;
            const size_t cell_count =
                static_cast<size_t>(grid.cells_x) * grid.cells_z;
            const uint32_t triangle_count =
                static_cast<uint32_t>(data.indices.size() / 3u);
            if (grid.cells_x == 0
                || grid.cells_z == 0
                || grid.cell_offsets.size() != cell_count + 1u
                || grid.cell_bounds.size() != cell_count
                || data.triangle_bounds.size() < triangle_count)
            {
                return SurfaceRayGridResult::Unavailable;
            }
            if (stats) {
                ++stats->grid_queries;
            }

            const wz::math::Vec3 ray_end =
                ray_origin + ray_direction * max_distance;
            wz::math::Vec3 local_origin{};
            wz::math::Vec3 local_end{};
            if (!inverse_affine_point(
                    entry.world_from_local,
                    ray_origin,
                    local_origin)
                || !inverse_affine_point(
                    entry.world_from_local,
                    ray_end,
                    local_end))
            {
                return SurfaceRayGridResult::Unavailable;
            }

            wz::math::Vec3 local_delta = local_end - local_origin;
            const float local_max_distance = wz::math::length(local_delta);
            if (local_max_distance <= 0.0f
                || !std::isfinite(local_max_distance))
            {
                return SurfaceRayGridResult::Miss;
            }
            wz::math::Vec3 local_direction =
                local_delta / local_max_distance;
            if (!normalize_checked(local_direction)) {
                return SurfaceRayGridResult::Miss;
            }

            const float grid_min_x = grid.origin_x;
            const float grid_max_x =
                grid.origin_x + grid.cell_size_x * grid.cells_x;
            const float grid_min_z = grid.origin_z;
            const float grid_max_z =
                grid.origin_z + grid.cell_size_z * grid.cells_z;

            float t_min = 0.0f;
            float t_max = local_max_distance;
            if (!ray_axis_clip(
                    local_origin.x,
                    local_direction.x,
                    grid_min_x,
                    grid_max_x,
                    t_min,
                    t_max)
                || !ray_axis_clip(
                    local_origin.z,
                    local_direction.z,
                    grid_min_z,
                    grid_max_z,
                    t_min,
                    t_max))
            {
                return SurfaceRayGridResult::Miss;
            }

            t_min = (std::max)(0.0f, t_min);
            t_max = (std::min)(local_max_distance, t_max);
            if (t_min > t_max) {
                return SurfaceRayGridResult::Miss;
            }

            const wz::math::Vec3 p0 =
                local_origin + local_direction * t_min;
            const wz::math::Vec3 p1 =
                local_origin + local_direction * t_max;
            const float min_x = (std::min)(p0.x, p1.x);
            const float max_x = (std::max)(p0.x, p1.x);
            const float min_z = (std::min)(p0.z, p1.z);
            const float max_z = (std::max)(p0.z, p1.z);

            const uint32_t cell_min_x = surface_grid_cell_index(
                min_x,
                grid.origin_x,
                grid.cell_size_x,
                grid.cells_x);
            const uint32_t cell_max_x = surface_grid_cell_index(
                max_x,
                grid.origin_x,
                grid.cell_size_x,
                grid.cells_x);
            const uint32_t cell_min_z = surface_grid_cell_index(
                min_z,
                grid.origin_z,
                grid.cell_size_z,
                grid.cells_z);
            const uint32_t cell_max_z = surface_grid_cell_index(
                max_z,
                grid.origin_z,
                grid.cell_size_z,
                grid.cells_z);

            bool hit = false;
            for (uint32_t z = cell_min_z; z <= cell_max_z; ++z) {
                for (uint32_t x = cell_min_x; x <= cell_max_x; ++x) {
                    if (stats) {
                        ++stats->grid_cells_visited;
                    }
                    const size_t cell =
                        static_cast<size_t>(z) * grid.cells_x + x;
                    const uint32_t begin = grid.cell_offsets[cell];
                    const uint32_t end = grid.cell_offsets[cell + 1u];
                    if (begin == end) {
                        continue;
                    }
                    if (!ray_bounds_hit(
                            grid.cell_bounds[cell],
                            local_origin,
                            local_direction,
                            local_max_distance))
                    {
                        if (stats) {
                            ++stats->grid_cells_rejected;
                        }
                        continue;
                    }

                    for (uint32_t i = begin; i < end; ++i) {
                        if (i >= grid.cell_triangle_indices.size()) {
                            continue;
                        }
                        const uint32_t tri = grid.cell_triangle_indices[i];
                        if (tri >= triangle_count
                            || tri >= data.triangle_bounds.size())
                        {
                            continue;
                        }
                        if (stats) {
                            ++stats->triangle_bounds_tested;
                        }
                        if (!ray_bounds_hit(
                                data.triangle_bounds[tri],
                                local_origin,
                                local_direction,
                                local_max_distance))
                        {
                            if (stats) {
                                ++stats->triangle_bounds_rejected;
                            }
                            continue;
                        }

                        const size_t index = static_cast<size_t>(tri) * 3u;
                        const uint32_t ia = data.indices[index + 0u];
                        const uint32_t ib = data.indices[index + 1u];
                        const uint32_t ic = data.indices[index + 2u];
                        if (ia >= data.points.size()
                            || ib >= data.points.size()
                            || ic >= data.points.size())
                        {
                            continue;
                        }

                        const wz::math::Vec3 a =
                            collision_point_position(data.points[ia]);
                        const wz::math::Vec3 b =
                            collision_point_position(data.points[ib]);
                        const wz::math::Vec3 c =
                            collision_point_position(data.points[ic]);

                        float local_distance = 0.0f;
                        wz::math::Vec3 local_position{};
                        wz::math::Vec3 local_normal{};
                        if (stats) {
                            ++stats->triangles_tested;
                        }
                        if (!ray_triangle_hit(
                                local_origin,
                                local_direction,
                                a,
                                b,
                                c,
                                local_max_distance,
                                local_distance,
                                local_position,
                                local_normal))
                        {
                            continue;
                        }

                        const wz::math::Vec3 world_position =
                            wz::math::mul_point(
                                entry.world_from_local,
                                local_position);
                        const float world_distance = wz::math::dot(
                            world_position - ray_origin,
                            ray_direction);
                        if (world_distance < 0.0f
                            || world_distance > max_distance
                            || world_distance >= best_distance)
                        {
                            continue;
                        }

                        const wz::math::Vec3 world_a =
                            wz::math::mul_point(entry.world_from_local, a);
                        const wz::math::Vec3 world_b =
                            wz::math::mul_point(entry.world_from_local, b);
                        const wz::math::Vec3 world_c =
                            wz::math::mul_point(entry.world_from_local, c);
                        wz::math::Vec3 world_normal =
                            wz::math::cross(world_b - world_a, world_c - world_a);
                        if (!normalize_checked(world_normal)) {
                            continue;
                        }
                        if (wz::math::dot(world_normal, ray_direction) >= 0.0f) {
                            world_normal.x = -world_normal.x;
                            world_normal.y = -world_normal.y;
                            world_normal.z = -world_normal.z;
                        }

                        best_distance = world_distance;
                        best_position = world_position;
                        best_normal = world_normal;
                        hit = true;
                    }
                }
            }

            return hit
                ? SurfaceRayGridResult::Hit
                : SurfaceRayGridResult::Miss;
        }

        void clear_surface_sample(WzSurfaceSample* out_sample) noexcept
        {
            if (!out_sample) {
                return;
            }
            *out_sample = WzSurfaceSample{
                .hit = 0u,
                .surface_entity =
                    static_cast<WzBehaviorEntityId>(
                        WZ_INVALID_BEHAVIOR_ENTITY),
                .position = WzVec3{},
                .normal = WzVec3{ .x = 0.0f, .y = 1.0f, .z = 0.0f },
            };
        }

        void fill_surface_sample(
            WzSurfaceSample* out_sample,
            WzBehaviorEntityId entity,
            const wz::math::Vec3& position,
            const wz::math::Vec3& normal) noexcept
        {
            *out_sample = WzSurfaceSample{
                .hit = 1u,
                .surface_entity = entity,
                .position = WzVec3{
                    .x = position.x,
                    .y = position.y,
                    .z = position.z,
                },
                .normal = WzVec3{
                    .x = normal.x,
                    .y = normal.y,
                    .z = normal.z,
                },
            };
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

                const SurfaceRayGridResult grid_result =
                    query_grid_surface_ray(
                        entry,
                        ray_origin,
                        ray_direction,
                        max_distance,
                        best_distance,
                        best_position,
                        best_normal,
                        context->surface_ray_stats);
                if (grid_result != SurfaceRayGridResult::Unavailable) {
                    if (grid_result == SurfaceRayGridResult::Hit) {
                        best_surface = entry.entity;
                    }
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

        uint8_t sample_terrain_surface(
            void* user,
            WzBehaviorEntityId terrain_entity,
            float world_x,
            float world_z,
            WzSurfaceSample* out_sample)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->frame_storage || !out_sample
                || !std::isfinite(world_x)
                || !std::isfinite(world_z))
            {
                return 0;
            }

            clear_surface_sample(out_sample);

            for (const auto& entry : context->frame_storage->collision.world) {
                if (entry.entity != terrain_entity
                    || !entry.enabled
                    || !entry.resolved
                    || !entry.resolved->occupancy.queryable)
                {
                    continue;
                }

                wz::engine::collision::CollisionSurfaceSample sample{};
                if (!wz::engine::collision::sample_terrain_surface(
                        entry,
                        world_x,
                        world_z,
                        sample)
                    || !sample.hit)
                {
                    return 0;
                }

                fill_surface_sample(
                    out_sample,
                    sample.surface_entity,
                    sample.position,
                    sample.normal);
                return 1;
            }

            return 0;
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

            // Accept an optional ":name" suffix for readability, e.g. "1:camera".
            // The id (before the first ':') is the authoritative stable key; when
            // a name follows, the node's CURRENT name must also match, so a typo
            // or a stale reference (after a rename) fails loudly instead of
            // resolving the wrong node. A plain id with no ':' matches as before.
            const std::string key{ authored_id };
            const std::size_t colon = key.find(':');
            const std::string id_part =
                (colon == std::string::npos) ? key : key.substr(0, colon);

            const auto it = context->scene->authored_to_runtime.find(id_part);
            if (it == context->scene->authored_to_runtime.end()) {
                return 0;
            }
            const WzBehaviorEntityId entity = it->second;

            if (colon != std::string::npos) {
                const std::string name_part = key.substr(colon + 1);
                const auto& names = context->scene->runtime_names;
                if (entity >= names.size() || names[entity] != name_part) {
                    return 0;  // ":name" doesn't match the node's current name
                }
            }

            *out_entity = entity;
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

        uint8_t find_descendant_by_name(
            void* user,
            WzBehaviorEntityId ancestor,
            const char* name,
            WzBehaviorEntityId* out_entity)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->scene || !name || !out_entity
                || !entity_valid(context->scene, ancestor))
            {
                return 0;
            }

            const auto& names = context->scene->runtime_names;
            const auto& graph = context->scene->storage.polytree;

            // First node in `ancestor`'s subtree whose name matches. Scoping to
            // the subtree (walk each candidate's parent chain up to `ancestor`)
            // is what makes this instance-safe: every spawned tank has a child
            // named "turret", but only THIS tank's turret descends from THIS
            // entity. `ancestor` itself is excluded (strict descendant).
            for (uint32_t i = 0; i < names.size(); ++i) {
                if (i == ancestor || names[i] != name) {
                    continue;
                }
                for (wz::scene::RuntimeEntityId cur =
                         wz::core::graph::parent(graph, i);
                     cur != wz::scene::INVALID_RUNTIME_ENTITY;
                     cur = wz::core::graph::parent(graph, cur))
                {
                    if (cur == ancestor) {
                        *out_entity = i;
                        return 1;
                    }
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

            auto* existing = context->behavior_state->find_instance_state(
                context->active_behavior->binding_id);
            if (existing && !existing->compatible(size, alignment)
                && context->logger)
            {
                context->logger->warn(
                    std::string("behavior instance state reset for binding '")
                    + context->active_behavior->binding_id
                    + "' because requested layout changed");
            }
            auto* block = context->behavior_state->allocate_instance_state(
                context->active_behavior->binding_id,
                size,
                alignment);
            return block ? block->data : nullptr;
        }

        void* alloc_instance_state_desc(
            void* user,
            const WzBehaviorStateDesc* desc)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->behavior_state
                || !context->active_behavior
                || context->active_behavior->binding_id.empty()
                || !desc)
            {
                return nullptr;
            }

            auto* existing = context->behavior_state->find_instance_state(
                context->active_behavior->binding_id);
            if (existing
                && !existing->compatible(
                    desc->size,
                    desc->alignment,
                    desc->layout_version)
                && context->logger)
            {
                context->logger->warn(
                    std::string("behavior instance state reset for binding '")
                    + context->active_behavior->binding_id
                    + "' because requested layout changed");
            }
            auto* block = context->behavior_state->allocate_instance_state(
                context->active_behavior->binding_id,
                desc->size,
                desc->alignment,
                desc->layout_version);
            return block ? block->data : nullptr;
        }

        void* create_shared_state(
            void* user,
            const char* key,
            uint32_t size,
            uint32_t alignment)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->behavior_state || !key || !*key) {
                return nullptr;
            }

            auto* existing = context->behavior_state->find_shared_state(key);
            if (existing && !existing->compatible(size, alignment)
                && context->logger)
            {
                context->logger->warn(
                    std::string("behavior shared state reset for key '")
                    + key
                    + "' because requested layout changed");
            }
            auto* block = context->behavior_state->allocate_shared_state(
                key,
                size,
                alignment);
            return block ? block->data : nullptr;
        }

        void* create_shared_state_desc(
            void* user,
            const char* key,
            const WzBehaviorStateDesc* desc)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->behavior_state || !key || !*key
                || !desc)
            {
                return nullptr;
            }

            auto* existing = context->behavior_state->find_shared_state(key);
            if (existing
                && !existing->compatible(
                    desc->size,
                    desc->alignment,
                    desc->layout_version)
                && context->logger)
            {
                context->logger->warn(
                    std::string("behavior shared state reset for key '")
                    + key
                    + "' because requested layout changed");
            }
            auto* block = context->behavior_state->allocate_shared_state(
                key,
                desc->size,
                desc->alignment,
                desc->layout_version);
            return block ? block->data : nullptr;
        }

        void* find_shared_state(void* user, const char* key)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->behavior_state || !key || !*key) {
                return nullptr;
            }

            auto* block =
                context->behavior_state->find_shared_state(key);
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

        // Find the quantum_agent binding on `entity` and return the POD state it
        // cached -- decoupled from the cognition store (an actuator reads the POD,
        // not the wave function). Null if the node hosts no quantum_agent.
        const QuantumAgentState* find_quantum_agent_state(
            BehaviorFrameContext* context,
            WzBehaviorEntityId entity)
        {
            if (!context || !context->scene || !context->behavior_state) {
                return nullptr;
            }
            for (const auto& record : context->scene->behaviors) {
                if (record.node != entity) {
                    continue;
                }
                const auto& component = record.component;
                if (component.module != kQuantumAgentModule
                    || component.binding_id.empty())
                {
                    continue;
                }
                const auto* block = context->behavior_state->find_instance_state(
                    component.binding_id);
                if (!block || !block->data) {
                    continue;
                }
                return static_cast<const QuantumAgentState*>(block->data);
            }
            return nullptr;
        }

        uint8_t get_agent_decision_at_query(
            void* user,
            WzBehaviorEntityId entity,
            uint32_t agent_index,
            WzAgentDecision* out)
        {
            if (!out) {
                return 0;
            }
            const QuantumAgentState* state = find_quantum_agent_state(
                static_cast<BehaviorFrameContext*>(user), entity);
            if (!state
                || agent_index >= state->agent_count
                || agent_index >= kQuantumAgentMaxDecisions)
            {
                return 0;
            }
            out->committed = state->committed[agent_index];
            out->marginal = state->marginal[agent_index];
            return 1;
        }

        uint8_t get_agent_decision_query(
            void* user,
            WzBehaviorEntityId entity,
            WzAgentDecision* out)
        {
            // Decision 0 -- the primary disposition.
            return get_agent_decision_at_query(user, entity, 0u, out);
        }

        uint8_t set_agent_goal_request(
            void* user,
            WzBehaviorEntityId entity,
            uint32_t agent_index,
            float field)
        {
            const QuantumAgentState* state = find_quantum_agent_state(
                static_cast<BehaviorFrameContext*>(user), entity);
            if (!state || state->handle == 0u) {
                return 0;
            }
            return quantum_agent_store().set_goal(
                       state->handle, agent_index, static_cast<double>(field))
                ? 1u
                : 0u;
        }

        uint8_t rearm_agent_request(
            void* user,
            WzBehaviorEntityId entity)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            const QuantumAgentState* state =
                find_quantum_agent_state(context, entity);
            if (!context || !state || state->handle == 0u) {
                return 0;
            }
            // Re-anneal from the current sim-time (the clock is sim-time based).
            return quantum_agent_store().rearm(state->handle, context->sim_time)
                ? 1u
                : 0u;
        }

        uint8_t set_next_wake_request(
            void* user,
            double delay_seconds)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->behavior_state
                || !context->active_behavior
                || context->active_behavior->binding_id.empty())
            {
                return 0;
            }

            // Relative delay -> absolute sim-time. A non-positive delay means
            // "as soon as possible" (the next cognition pass): clamp to now.
            const double when =
                context->sim_time
                + (delay_seconds > 0.0 ? delay_seconds : 0.0);
            context->behavior_state->set_next_wake(
                context->active_behavior->binding_id, when);
            return 1;
        }

        uint8_t spawn_child_request(
            void* user,
            WzBehaviorEntityId parent_entity)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->authoring || !context->scene) {
                return 0;
            }

            // Resolve the behavior's runtime entity id to the parent's authored
            // scene-node id (what the add-child apply path consumes) using the
            // same runtime->authored mapping find_entity_by_authored_id rides in
            // reverse. Queuing the authored id keeps the request stable across a
            // structural rebuild that would renumber runtime entities.
            if (parent_entity >= context->scene->runtime_to_authored.size()) {
                return 0;
            }
            context->authoring->spawn_child_parents.push_back(
                context->scene->runtime_to_authored[parent_entity]);
            return 1;
        }

        uint8_t remove_node_request(
            void* user,
            WzBehaviorEntityId entity)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->authoring || !context->scene) {
                return 0;
            }

            // Resolve runtime entity -> authored scene-node id (fail-closed),
            // exactly as spawn_child_request. The authored id stays valid even
            // as a prior drain entry renumbers runtime entities.
            if (entity >= context->scene->runtime_to_authored.size()) {
                return 0;
            }
            context->authoring->remove_node_targets.push_back(
                context->scene->runtime_to_authored[entity]);
            return 1;
        }

        uint8_t set_renderable_asset_request(
            void* user,
            WzBehaviorEntityId entity,
            uint64_t asset_graph_node_id)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->authoring || !context->scene) {
                return 0;
            }

            // Resolve runtime entity -> authored scene-node id (fail-closed),
            // exactly as spawn_child_request, and pair it with the asset-graph
            // node id to bind (0 clears the renderable).
            if (entity >= context->scene->runtime_to_authored.size()) {
                return 0;
            }
            context->authoring->set_renderable_requests.push_back(
                BehaviorSetRenderableRequest{
                    .node_id = context->scene->runtime_to_authored[entity],
                    .asset_graph_node_id = asset_graph_node_id,
                });
            return 1;
        }

        uint8_t reparent_node_request(
            void* user,
            WzBehaviorEntityId entity,
            WzBehaviorEntityId new_parent_entity)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->authoring || !context->scene) {
                return 0;
            }

            // Resolve the node to reparent (fail-closed), exactly as
            // spawn_child_request. The authored id stays valid even as a prior
            // drain entry renumbers runtime entities.
            if (entity >= context->scene->runtime_to_authored.size()) {
                return 0;
            }

            // The top-level sentinel is NOT an out-of-range runtime id: it means
            // "detach to top level". Map it to an empty authored parent id (what
            // reparent_node treats as no parent) BEFORE the bounds check, so it
            // is never rejected. Any other parent must resolve fail-closed.
            wz::scene::AuthoredEntityId new_parent_id;
            if (new_parent_entity != WZ_INVALID_BEHAVIOR_ENTITY) {
                if (new_parent_entity
                    >= context->scene->runtime_to_authored.size())
                {
                    return 0;
                }
                new_parent_id =
                    context->scene->runtime_to_authored[new_parent_entity];
            }

            context->authoring->reparent_requests.push_back(
                BehaviorReparentRequest{
                    .node_id = context->scene->runtime_to_authored[entity],
                    .new_parent_id = new_parent_id,
                });
            return 1;
        }

        uint8_t add_node_component_request(
            void* user,
            WzBehaviorEntityId entity,
            const char* kind)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->authoring || !context->scene || !kind) {
                return 0;
            }

            // Resolve runtime entity -> authored scene-node id (fail-closed),
            // exactly as spawn_child_request. The kind pointer is valid only
            // during this call (the request is deferred to the frame boundary),
            // so COPY it into the queued request rather than storing the raw
            // pointer.
            if (entity >= context->scene->runtime_to_authored.size()) {
                return 0;
            }
            context->authoring->add_component_requests.push_back(
                BehaviorComponentRequest{
                    .node_id = context->scene->runtime_to_authored[entity],
                    .kind = kind,
                });
            return 1;
        }

        uint8_t remove_node_component_request(
            void* user,
            WzBehaviorEntityId entity,
            const char* kind)
        {
            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context || !context->authoring || !context->scene || !kind) {
                return 0;
            }

            // Same fail-closed resolve + kind copy as add_node_component_request
            // (the kind pointer is transient; the request is drained later).
            if (entity >= context->scene->runtime_to_authored.size()) {
                return 0;
            }
            context->authoring->remove_component_requests.push_back(
                BehaviorComponentRequest{
                    .node_id = context->scene->runtime_to_authored[entity],
                    .kind = kind,
                });
            return 1;
        }

        uint8_t submit_gpu_compute_job(
            void* user,
            const WzGpuComputeJobDesc* job,
            WzGpuWorkId* out_work)
        {
            if (!user || !job) {
                return 0u;
            }

            auto* context = static_cast<BehaviorFrameContext*>(user);
            if (!context->gpu_compute) {
                return 0u;
            }

            return context->gpu_compute->submit(
                context->active_entity,
                *job,
                out_work)
                ? uint8_t{ 1 }
                : uint8_t{ 0 };
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
                .sample_terrain_surface = sample_terrain_surface,
                .timing = timing,
                .scene_query_user = &context,
                .find_entity_by_name = find_entity_by_name,
                .find_entity_by_authored_id = find_entity_by_authored_id,
                .behavior_config_user = &context,
                .get_config_bool = get_config_bool,
                .get_config_number = get_config_number,
                .get_config_string = get_config_string,
                .active_input_event = context.active_input_payload,
                .active_gpu_compute_event =
                    context.active_gpu_compute_payload,
                .behavior_state_user = &context,
                .get_instance_state = get_instance_state,
                .find_shared_state = find_shared_state,
                .gpu_compute_user = &context,
                .submit_gpu_compute = submit_gpu_compute_job,
                .deferred_authoring_user = &context,
                .spawn_child = spawn_child_request,
                .remove_node = remove_node_request,
                .set_renderable_asset = set_renderable_asset_request,
                .reparent_node = reparent_node_request,
                .add_node_component = add_node_component_request,
                .remove_node_component = remove_node_component_request,
                .sim_time = context.sim_time,
                .wake_scheduler_user = &context,
                .set_next_wake = set_next_wake_request,
                .cognition_reader_user = &context,
                .get_agent_decision = get_agent_decision_query,
                .find_descendant_by_name = find_descendant_by_name,
                .get_agent_decision_at = get_agent_decision_at_query,
                .set_agent_goal = set_agent_goal_request,
                .rearm_agent = rearm_agent_request,
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
                .sample_terrain_surface = sample_terrain_surface,
                .timing = timing,
                .scene_query_user = &context,
                .find_entity_by_name = find_entity_by_name,
                .find_entity_by_authored_id = find_entity_by_authored_id,
                .behavior_config_user = &context,
                .get_config_bool = get_config_bool,
                .get_config_number = get_config_number,
                .get_config_string = get_config_string,
                .active_input_event = context.active_input_payload,
                .active_gpu_compute_event =
                    context.active_gpu_compute_payload,
                .behavior_state_user = &context,
                .get_instance_state = get_instance_state,
                .find_shared_state = find_shared_state,
                .gpu_compute_user = &context,
                .submit_gpu_compute = submit_gpu_compute_job,
                .deferred_authoring_user = &context,
                .spawn_child = spawn_child_request,
                .remove_node = remove_node_request,
                .set_renderable_asset = set_renderable_asset_request,
                .reparent_node = reparent_node_request,
                .add_node_component = add_node_component_request,
                .remove_node_component = remove_node_component_request,
                .sim_time = context.sim_time,
                .wake_scheduler_user = &context,
                .set_next_wake = set_next_wake_request,
                .cognition_reader_user = &context,
                .get_agent_decision = get_agent_decision_query,
                .find_descendant_by_name = find_descendant_by_name,
                .get_agent_decision_at = get_agent_decision_at_query,
                .set_agent_goal = set_agent_goal_request,
                .rearm_agent = rearm_agent_request,
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
                .create_shared_state = create_shared_state,
                .find_shared_state = find_shared_state,
                .alloc_instance_state_desc = alloc_instance_state_desc,
                .create_shared_state_desc = create_shared_state_desc,
                .find_descendant_by_name = find_descendant_by_name,
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

        BehaviorParamType from_abi_param_type(
            WzBehaviorParamType type) noexcept
        {
            switch (type) {
            case WZ_BEHAVIOR_PARAM_FLOAT:
                return BehaviorParamType::Float;
            case WZ_BEHAVIOR_PARAM_BOOL:
                return BehaviorParamType::Bool;
            case WZ_BEHAVIOR_PARAM_STRING:
                return BehaviorParamType::String;
            case WZ_BEHAVIOR_PARAM_NONE:
            default:
                return BehaviorParamType::None;
            }
        }

        uint8_t register_behavior_desc(
            void* user,
            const WzBehaviorDesc* desc)
        {
            // Size-gated optional fields, mirroring register_module_desc: a
            // host reads only the fields the caller's `size` actually covers.
            const uint32_t required_size =
                static_cast<uint32_t>(offsetof(WzBehaviorDesc, params));
            const bool has_params =
                desc
                && desc->size
                    >= offsetof(WzBehaviorDesc, param_count)
                        + sizeof(desc->param_count);

            auto* context = static_cast<RegisterContext*>(user);
            if (!context || !context->registry || !context->host
                || !desc || desc->size < required_size
                || !desc->name || !desc->function)
            {
                return 0;
            }

            std::vector<BehaviorParamSpec> params;
            const uint32_t param_count =
                has_params ? desc->param_count : 0u;
            params.reserve(param_count);
            for (uint32_t i = 0; i < param_count; ++i) {
                if (!desc->params || !desc->params[i].key
                    || desc->params[i].key[0] == '\0')
                {
                    continue;
                }
                const WzBehaviorParamDesc& param = desc->params[i];
                params.push_back(BehaviorParamSpec{
                    .key = param.key,
                    .label = param.label ? param.label : "",
                    .type = from_abi_param_type(param.type),
                    .default_number = param.default_number,
                    .default_string =
                        param.default_string ? param.default_string : "",
                });
            }

            auto* binding_ptr = context->host->add_binding(
                desc->function,
                desc->behavior_user_data,
                context->logger);
            const BehaviorHandle handle =
                context->registry->register_behavior(
                    desc->module ? desc->module : "",
                    desc->name,
                    dispatch_abi_behavior,
                    std::move(params),
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
            const bool has_params =
                desc
                && desc->size
                    >= offsetof(WzBehaviorModuleDesc, param_count)
                        + sizeof(desc->param_count);
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
            // Declared config params (size-gated; mirrors register_behavior_desc).
            std::vector<BehaviorParamSpec> params;
            const uint32_t param_count =
                has_params ? desc->param_count : 0u;
            params.reserve(param_count);
            for (uint32_t i = 0; i < param_count; ++i) {
                if (!desc->params || !desc->params[i].key
                    || desc->params[i].key[0] == '\0')
                {
                    continue;
                }
                const WzBehaviorParamDesc& param = desc->params[i];
                params.push_back(BehaviorParamSpec{
                    .key = param.key,
                    .label = param.label ? param.label : "",
                    .type = from_abi_param_type(param.type),
                    .default_number = param.default_number,
                    .default_string =
                        param.default_string ? param.default_string : "",
                });
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
                    std::move(params),
                    binding_ptr);
            return handle.valid() ? uint8_t{ 1 } : uint8_t{ 0 };
        }

        uint8_t register_gpu_kernel_contract(
            void* user,
            const WzGpuKernelContractDesc* desc)
        {
            auto* context = static_cast<RegisterContext*>(user);
            if (!context || !context->registry || !desc
                || !desc->kernel_id || !desc->kernel_id[0]
                || !desc->ports || desc->port_count == 0u)
            {
                return 0;
            }

            BehaviorGpuKernelContract contract{};
            contract.kernel_id = desc->kernel_id;
            contract.ports.reserve(desc->port_count);
            for (uint32_t i = 0; i < desc->port_count; ++i) {
                const WzGpuKernelPortContractDesc& src = desc->ports[i];
                if (!src.name || !src.name[0]
                    || src.kind == WZ_GPU_PORT_NONE
                    || src.direction == 0u)
                {
                    return 0;
                }
                contract.ports.push_back({
                    .name = src.name,
                    .kind = src.kind,
                    .direction = src.direction,
                    .stride_bytes = src.stride_bytes,
                });
            }

            return context->registry->register_gpu_kernel_contract(
                std::move(contract))
                ? uint8_t{ 1 }
                : uint8_t{ 0 };
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
            .register_gpu_kernel_contract =
                register_gpu_kernel_contract,
            .register_behavior_desc = register_behavior_desc,
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

    BehaviorPluginHost::DynamicLoadResult
    BehaviorPluginHost::reload_dynamic_module(
        BehaviorRegistry& registry,
        const std::filesystem::path& path,
        wz::Logger* logger,
        const char* register_symbol)
    {
#if defined(_WIN32)
        std::vector<std::size_t> old_indices;
        for (std::size_t i = 0; i < dynamic_modules_.size(); ++i) {
            if (same_dynamic_module_path(dynamic_modules_[i], path)) {
                old_indices.push_back(i);
            }
        }

        DynamicLoadResult result =
            load_dynamic_module(registry, path, logger, register_symbol);
        if (!result.ok()) {
            return result;
        }

        for (std::size_t i = old_indices.size(); i > 0u; --i) {
            const std::size_t index = old_indices[i - 1u];
            unload_dynamic_module_copy(dynamic_modules_[index]);
            dynamic_modules_.erase(dynamic_modules_.begin() + index);
        }
        return result;
#else
        return load_dynamic_module(registry, path, logger, register_symbol);
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
                reload_dynamic_module(registry, path, logger);
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
        for (DynamicModule& module : dynamic_modules_) {
            unload_dynamic_module_copy(module);
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
