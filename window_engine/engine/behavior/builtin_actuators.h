#pragma once

// engine/behavior/builtin_actuators.h
//
// Builtin statechart ACTUATORS (v33) -- named leaf functions a statechart `call`
// effect invokes to drive the world (the open actuator vocabulary). They are
// registered through the SAME WzBehaviorPluginApi::register_actuator a project
// plugin uses, so the builtins dogfood the exact path an author's own actuator
// would take. `move_toward` is the first locomotion primitive: it turns a chart's
// decision ("go to the prey") into motion through the ordinary behavior facts.

#include <engine/behavior/behavior_plugin_abi.h>

namespace wz::engine::behavior
{
    // Register the builtin actuator pack (move_toward, ...). Returns 0 if the api is
    // null, version-mismatched, or predates register_actuator.
    uint8_t register_builtin_actuators(WzBehaviorPluginApi* api);
}
