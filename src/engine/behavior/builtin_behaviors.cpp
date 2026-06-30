#include <engine/behavior/builtin_behaviors.h>
#include <engine/behavior/drive_forward_behaviors.h>
#include <engine/behavior/prefab_spawner_behaviors.h>
#include <engine/behavior/sample_collision_behaviors.h>
#include <engine/behavior/scene_camera_behaviors.h>

#include <logging/logger.h>

#include <sstream>

namespace wz::engine::behavior
{
    namespace
    {
        const char* collision_kind_name(WzCollisionEventKind kind) noexcept
        {
            switch (kind) {
            case WZ_COLLISION_EVENT_ENTER:
                return "enter";
            case WZ_COLLISION_EVENT_STAY:
                return "stay";
            case WZ_COLLISION_EVENT_EXIT:
                return "exit";
            }
            return "unknown";
        }

        void log_collision_events(
            const WzBehaviorFrameFacts* facts,
            WzBehaviorEntityId entity,
            void*)
        {
            if (!facts || !facts->log_info
                || !facts->collision_events.read)
            {
                return;
            }

            for (uint32_t i = 0; i < facts->collision_events.count; ++i) {
                WzCollisionEntityEvent event{};
                if (!facts->collision_events.read(
                        facts->collision_events.user,
                        i,
                        &event))
                {
                    continue;
                }
                if (event.entity != entity) {
                    continue;
                }

                std::ostringstream msg;
                msg
                    << "[behavior/debug] collision."
                    << collision_kind_name(event.kind)
                    << " entity=" << event.entity
                    << " other=" << event.other
                    << " self_trigger="
                    << (event.self_is_trigger != 0 ? "true" : "false");
                facts->log_info(facts->log_user, msg.str().c_str());
            }
        }

        uint8_t register_debug_pack(WzBehaviorPluginApi* api)
        {
            if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
                || !api->register_behavior)
            {
                return 0;
            }

            return api->register_behavior(
                api->user,
                kDebugBehaviorModule,
                kLogCollisionEventsBehavior,
                log_collision_events,
                nullptr);
        }
    }

    void register_builtin_behaviors(
        BehaviorRegistry& registry,
        BehaviorPluginHost& plugins,
        wz::Logger& logger)
    {
        const bool registered_debug =
            plugins.register_static_pack(registry, register_debug_pack, &logger);
        const bool registered_sample =
            plugins.register_static_pack(
                registry,
                register_sample_collision_behaviors,
                &logger);
        const bool registered_scene_camera =
            plugins.register_static_pack(
                registry,
                register_scene_camera_behaviors,
                &logger);
        const bool registered_prefab_spawner =
            plugins.register_static_pack(
                registry,
                register_prefab_spawner_behaviors,
                &logger);
        const bool registered_drive_forward =
            plugins.register_static_pack(
                registry,
                register_drive_forward_behaviors,
                &logger);
        if (!registered_debug) {
            logger.warn(
                "[behavior] failed to register builtin behavior pack: debug");
        }
        if (!registered_sample) {
            logger.warn(
                "[behavior] failed to register builtin behavior pack: sample");
        }
        if (!registered_scene_camera) {
            logger.warn(
                "[behavior] failed to register builtin behavior pack: "
                "scene_camera");
        }
        if (!registered_prefab_spawner) {
            logger.warn(
                "[behavior] failed to register builtin behavior pack: "
                "prefab_spawner");
        }
        if (!registered_drive_forward) {
            logger.warn(
                "[behavior] failed to register builtin behavior pack: "
                "drive_forward");
        }
    }
}
