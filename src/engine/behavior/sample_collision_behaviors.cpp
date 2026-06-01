#include <engine/behavior/sample_collision_behaviors.h>

namespace wz::engine::behavior
{
    namespace
    {
        void bounce_on_collision_enter(
            const WzBehaviorFrameFacts* facts,
            WzBehaviorEntityId entity,
            void*)
        {
            if (!facts || !facts->write_command
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
                if (event.entity != entity
                    || event.kind != WZ_COLLISION_EVENT_ENTER)
                {
                    continue;
                }

                const WzBehaviorCommand command{
                    .entity = entity,
                    .kind = WZ_BEHAVIOR_COMMAND_ADD_LOCAL_TRANSLATION,
                    .values = { 0.0f, 1.0f, 0.0f, 0.0f },
                };
                facts->write_command(
                    facts->command_writer_user,
                    &command);
            }
        }
    }

    uint8_t register_sample_collision_behaviors(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_behavior)
        {
            return 0;
        }

        return api->register_behavior(
            api->user,
            kSampleBehaviorModule,
            kBounceOnCollisionEnterBehavior,
            bounce_on_collision_enter,
            nullptr);
    }
}
