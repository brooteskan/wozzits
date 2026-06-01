#include <engine/behavior/behavior_command_apply.h>

#include <algorithm>
#include <cmath>
#include <math/mat4.h>
#include <scene/scene_graph.h>

namespace wz::engine::behavior
{
    namespace
    {
        bool entity_valid(
            const wz::engine::assets::SceneInstance& scene,
            wz::scene::RuntimeEntityId entity) noexcept
        {
            return entity != wz::scene::INVALID_RUNTIME_ENTITY
                && entity < wz::core::graph::node_count(
                    scene.storage.polytree);
        }

        float column_length(
            const wz::math::Mat4& matrix,
            uint32_t column) noexcept
        {
            const uint32_t i = column * 4u;
            return std::sqrt(
                matrix.m[i + 0u] * matrix.m[i + 0u]
                + matrix.m[i + 1u] * matrix.m[i + 1u]
                + matrix.m[i + 2u] * matrix.m[i + 2u]);
        }

        void set_column_scale(
            wz::math::Mat4& matrix,
            uint32_t column,
            float scale) noexcept
        {
            const uint32_t i = column * 4u;
            const float length = column_length(matrix, column);
            if (length > 0.0f) {
                const float factor = scale / length;
                matrix.m[i + 0u] *= factor;
                matrix.m[i + 1u] *= factor;
                matrix.m[i + 2u] *= factor;
                return;
            }

            matrix.m[i + 0u] = column == 0u ? scale : 0.0f;
            matrix.m[i + 1u] = column == 1u ? scale : 0.0f;
            matrix.m[i + 2u] = column == 2u ? scale : 0.0f;
        }

        void set_local_scale(
            wz::math::Mat4& matrix,
            float x,
            float y,
            float z) noexcept
        {
            set_column_scale(matrix, 0u, x);
            set_column_scale(matrix, 1u, y);
            set_column_scale(matrix, 2u, z);
        }

        void add_local_scale(
            wz::math::Mat4& matrix,
            float x,
            float y,
            float z) noexcept
        {
            set_local_scale(
                matrix,
                column_length(matrix, 0u) + x,
                column_length(matrix, 1u) + y,
                column_length(matrix, 2u) + z);
        }

        void set_local_rotation(
            wz::math::Mat4& matrix,
            const wz::math::Quaternion& rotation) noexcept
        {
            const float sx = column_length(matrix, 0u);
            const float sy = column_length(matrix, 1u);
            const float sz = column_length(matrix, 2u);
            const wz::math::Mat4 r = wz::math::rotation(rotation);

            for (uint32_t row = 0; row < 3u; ++row) {
                matrix.m[0u * 4u + row] = r.m[0u * 4u + row] * sx;
                matrix.m[1u * 4u + row] = r.m[1u * 4u + row] * sy;
                matrix.m[2u * 4u + row] = r.m[2u * 4u + row] * sz;
            }
            matrix.m[3u] = 0.0f;
            matrix.m[7u] = 0.0f;
            matrix.m[11u] = 0.0f;
            matrix.m[15u] = 1.0f;
        }
    }

    uint32_t apply_behavior_commands(
        wz::engine::assets::SceneInstance& scene,
        std::span<const BehaviorCommand> commands,
        std::vector<wz::scene::RuntimeEntityId>* out_changed_entities)
    {
        uint32_t applied = 0;
        if (out_changed_entities) {
            out_changed_entities->clear();
        }

        for (const BehaviorCommand& command : commands) {
            if (!entity_valid(scene, command.entity)) {
                continue;
            }

            // Scene graph node_data exposes a const view; command application is
            // the narrow mutation point that applies deferred behavior writes.
            auto& node = const_cast<wz::scene::TransformNode&>(
                wz::core::graph::node_data(
                    scene.storage.polytree,
                    command.entity));

            switch (command.kind) {
            case BehaviorCommandKind::AddLocalTranslation:
                node.local.m[12] += command.values[0];
                node.local.m[13] += command.values[1];
                node.local.m[14] += command.values[2];
                ++applied;
                if (out_changed_entities) {
                    out_changed_entities->push_back(command.entity);
                }
                break;

            case BehaviorCommandKind::SetLocalTranslation:
                node.local.m[12] = command.values[0];
                node.local.m[13] = command.values[1];
                node.local.m[14] = command.values[2];
                ++applied;
                if (out_changed_entities) {
                    out_changed_entities->push_back(command.entity);
                }
                break;

            case BehaviorCommandKind::AddLocalScale:
                add_local_scale(
                    node.local,
                    command.values[0],
                    command.values[1],
                    command.values[2]);
                ++applied;
                if (out_changed_entities) {
                    out_changed_entities->push_back(command.entity);
                }
                break;

            case BehaviorCommandKind::SetLocalScale:
                set_local_scale(
                    node.local,
                    command.values[0],
                    command.values[1],
                    command.values[2]);
                ++applied;
                if (out_changed_entities) {
                    out_changed_entities->push_back(command.entity);
                }
                break;

            case BehaviorCommandKind::SetLocalRotation:
                set_local_rotation(
                    node.local,
                    wz::math::Quaternion{
                        command.values[0],
                        command.values[1],
                        command.values[2],
                        command.values[3],
                    });
                ++applied;
                if (out_changed_entities) {
                    out_changed_entities->push_back(command.entity);
                }
                break;

            case BehaviorCommandKind::None:
                break;
            }
        }

        if (applied != 0) {
            if (out_changed_entities) {
                std::sort(
                    out_changed_entities->begin(),
                    out_changed_entities->end());
                out_changed_entities->erase(
                    std::unique(
                        out_changed_entities->begin(),
                        out_changed_entities->end()),
                    out_changed_entities->end());
            }
            wz::scene::propagate_all(scene.storage.polytree);
        }

        return applied;
    }
}
