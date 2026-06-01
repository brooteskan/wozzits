#pragma once

// engine/behavior/builtin_behaviors.h

#include <engine/behavior/behavior_registry.h>

namespace wz
{
    struct Logger;
}

namespace wz::engine::behavior
{
    inline constexpr const char* kDebugBehaviorModule = "debug";
    inline constexpr const char* kLogCollisionEventsBehavior =
        "log_collision_events";

    void register_builtin_behaviors(
        BehaviorRegistry& registry,
        wz::Logger& logger);
}
