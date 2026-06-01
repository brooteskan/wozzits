#include <engine/behavior/builtin_behaviors.h>

#include <engine/collision/collision_frame.h>
#include <engine/frame_storage.h>

#include <logging/logger.h>

#include <sstream>

namespace wz::engine::behavior
{
    namespace
    {
        const char* collision_kind_name(
            wz::engine::collision::CollisionEventKind kind) noexcept
        {
            using Kind = wz::engine::collision::CollisionEventKind;
            switch (kind) {
            case Kind::Enter:
                return "enter";
            case Kind::Stay:
                return "stay";
            case Kind::Exit:
                return "exit";
            }
            return "unknown";
        }

        void log_collision_events(
            BehaviorFrameContext& context,
            wz::scene::RuntimeEntityId entity,
            void* user_data)
        {
            auto* logger = static_cast<wz::Logger*>(user_data);
            if (!logger || !context.frame_storage) {
                return;
            }

            for (const auto& event :
                context.frame_storage->collision.routed_entity_events)
            {
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
                    << (event.self_is_trigger ? "true" : "false");
                logger->info(msg.str());
            }
        }
    }

    void register_builtin_behaviors(
        BehaviorRegistry& registry,
        wz::Logger& logger)
    {
        registry.register_behavior(
            kDebugBehaviorModule,
            kLogCollisionEventsBehavior,
            log_collision_events,
            &logger);
    }
}
