// enemy_tank_v1_actuators.cpp
//
// The body's LEVERS: statechart-callable actuators that write enemy_tank_v1's
// intent knobs through the peer-state channel. A chart's Call effect invokes them
// by name with resolved args. No behavior module in this DLL -- it contributes
// only actuators, the same shape as tank_actuators_plugin.cpp.

#include <engine/behavior/behavior_module_api.h>

#include "tank_state.h"

namespace
{
    // The tank's own instance state on this node, fetched as a PEER --
    // wz_instance_state<T> here would be the CALLING runner's block, not the tank's.
    TankState* tank_state(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self)
    {
        return wz_instance_state_of<TankState>(facts, self, kEnemyTankModule);
    }

    // choose_target(slot): point the tank's intent at PACKED target `slot`.
    // Out-of-range slots are IGNORED and the current choice stands -- the body does
    // not trust its callers. A mind in training will hand it nonsense.
    void choose_target(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self,
        const WzActuatorArg* args, uint32_t n, void*)
    {
        if (n < 1 || args[0].kind != WZ_ACTUATOR_ARG_SCALAR) {
            return;
        }
        TankState* s = tank_state(facts, self);
        if (!s) {
            return;
        }
        const double slot = args[0].scalar;
        if (slot >= 0.0 && slot < (double)s->target_count) {
            s->target_chosen = (uint8_t)slot;
        }
    }

    constexpr WzActuatorParamDesc kChooseParams[] = {
        { "slot", WZ_ACTUATOR_PARAM_SCALAR, 0.0 },
    };

    // set_throttle(value): HOW FAST, 0..1 -- the second knob, and the one a mind
    // has to learn rather than pick. Clamped at the point of use for the reason
    // Chapter 4 gave: a mind in training hands you 1.4 and -0.2 on its way to
    // being trained, and a body that trusts its mind is a body that teleports.
    void set_throttle(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self,
        const WzActuatorArg* args, uint32_t n, void*)
    {
        if (n < 1 || args[0].kind != WZ_ACTUATOR_ARG_SCALAR) {
            return;
        }
        TankState* s = tank_state(facts, self);
        if (!s) {
            return;
        }
        double v = args[0].scalar;
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        s->throttle = (float)v;
        wz_log_infof(facts, "[enemy_tank_v1] set throttle %.2f", (float)v);
    }

    constexpr WzActuatorParamDesc kThrottleParams[] = {
        { "value", WZ_ACTUATOR_PARAM_SCALAR, 1.0 },
    };
}

extern "C" WZ_BEHAVIOR_MODULE_EXPORT uint8_t wz_register_behaviors(
    WzBehaviorPluginApi* api)
{
    if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
        || !api->register_actuator)
    {
        return 0;
    }

    WzBehaviorActuatorDesc desc{};
    desc.size = sizeof(desc);
    desc.name = "choose_target";
    desc.label = "Choose target (packed slot)";
    desc.params = kChooseParams;
    desc.param_count = 1u;
    desc.fn = choose_target;
    api->register_actuator(api->user, &desc);

    WzBehaviorActuatorDesc throttle{};
    throttle.size = sizeof(throttle);
    throttle.name = "set_throttle";
    throttle.label = "Set throttle (0-1)";
    throttle.params = kThrottleParams;
    throttle.param_count = 1u;
    throttle.fn = set_throttle;
    api->register_actuator(api->user, &throttle);
    return 2;
}