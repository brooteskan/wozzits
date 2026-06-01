#pragma once

// engine/behavior/sample_collision_behaviors.h

#include <engine/behavior/behavior_plugin_abi.h>

namespace wz::engine::behavior
{
    inline constexpr const char* kSampleBehaviorModule = "sample";
    inline constexpr const char* kBounceOnCollisionEnterBehavior =
        "bounce_on_collision_enter";

    uint8_t register_sample_collision_behaviors(WzBehaviorPluginApi* api);
}
