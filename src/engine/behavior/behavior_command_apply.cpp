#include <engine/behavior/behavior_command_apply.h>

#include <algorithm>
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
