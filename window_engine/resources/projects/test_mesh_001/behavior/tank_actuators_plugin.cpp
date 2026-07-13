// behavior/tank_actuators_plugin.cpp
//
// The enemy tank + commander ACTIONS, exposed as REGISTERED ACTUATORS (behavior ABI
// v33) so a cognition STATECHART can bind them: the chart reads the tank's quantum
// agent (marginal/committed) and `call`s these to drive / aim / fire / blink, and the
// commander's chart calls request_reinforcement. This is the "actuators-first" slice
// of moving the tank/commander mode logic into statecharts -- the ACTIONS become
// bindable functions; the deciding/sensing/learning stay in the C++ modules.
//
// Each actuator is handed `self` (the acting node) directly, but the shared tank_drive
// / cannon_fire / teleport helpers are written around a behavior EVENT (self via
// wz_self). So we fabricate a minimal self-event and reuse those helpers UNCHANGED --
// an actuator drives with the exact same feel as the tank's own imperative dispatch.
// The stateful actions (aim/fire/blink) reach the tank's handles + sub-machine state
// through the PEER read wz_instance_state_of<QuantumTankState>(self,
// "quantum_tank_agent"); their per-frame ADVANCE (cannon flight, teleport animation)
// still runs in the quantum_tank_agent module -- an actuator only TRIGGERS them.
//
// Registers ONLY actuators (no behavior module): a hand-written wz_register_behaviors.

#include <engine/behavior/behavior_module_api.h>

#include "agent_tank.h"     // QuantumTankState + tank_drive/cannon_fire/teleport/lifecycle
#include "agent_tank_config.h"   // kFireCooldown (the shared reload interval)
#include "squad_deploy.h"

namespace
{
    // How hard the hull yaws toward its heading error (matches the tank's steering).
    constexpr float kSteerGain = 3.0f;
    // Aim a touch above the target's origin so the shot strikes the hull, not its feet.
    constexpr float kAimHeight = 1.0f;
    constexpr float kDefaultStandoff = 30.0f;

    // A self-event so the tank_drive helpers (written around wz_self / the event) work
    // from an actuator, which is handed `self` directly.
    WzBehaviorEvent self_event(WzBehaviorEntityId self)
    {
        WzBehaviorEvent ev{};
        ev.kind = WZ_EVENT_FRAME_UPDATE;
        ev.entity = self;
        return ev;
    }

    // The tank's own instance state (handles + sub-machines) on this node, read as a
    // peer -- wz_instance_state<T> here would be the runner's, not the tank's.
    QuantumTankState* tank_state(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self)
    {
        return wz_instance_state_of<QuantumTankState>(
            facts, self, "quantum_tank_agent");
    }

    // pursue(target, speed): turn the nose to the target and drive forward.
    void pursue(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self,
        const WzActuatorArg* args, uint32_t n, void*)
    {
        if (n < 1 || args[0].kind != WZ_ACTUATOR_ARG_ENTITY) {
            return;
        }
        const WzBehaviorEntityId target = args[0].entity;
        const float speed =
            (n >= 2) ? static_cast<float>(args[1].scalar) : tank_drive::kMoveSpeed;
        WzBehaviorEvent ev = self_event(self);
        const float heading = tank_drive::face_yaw_rate(
            facts, &ev, target, kSteerGain, tank_drive::kTurnSpeed);
        tank_drive::drive_facing(facts, &ev, heading, speed);
    }

    // orbit(target, standoff, speed): circle the target at ~standoff world units.
    void orbit(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self,
        const WzActuatorArg* args, uint32_t n, void*)
    {
        if (n < 1 || args[0].kind != WZ_ACTUATOR_ARG_ENTITY) {
            return;
        }
        const WzBehaviorEntityId target = args[0].entity;
        const float standoff =
            (n >= 2) ? static_cast<float>(args[1].scalar) : kDefaultStandoff;
        const float speed =
            (n >= 3) ? static_cast<float>(args[2].scalar) : tank_drive::kMoveSpeed;
        WzBehaviorEvent ev = self_event(self);
        const float heading = tank_drive::orbit_yaw_rate(
            facts, &ev, target, standoff, kSteerGain, tank_drive::kTurnSpeed);
        tank_drive::drive_facing(facts, &ev, heading, speed);
    }

    // halt(): stop moving (a HOLD state's drive).
    void halt(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self,
        const WzActuatorArg*, uint32_t, void*)
    {
        WzBehaviorEvent ev = self_event(self);
        tank_drive::drive_facing(facts, &ev, 0.0f, 0.0f);
    }

    // aim_at(target): slew the turret + elevate the gun onto the target, clamped to the
    // turret's frontal arc and the gun's elevation limits.
    void aim_at(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self,
        const WzActuatorArg* args, uint32_t n, void*)
    {
        if (n < 1 || args[0].kind != WZ_ACTUATOR_ARG_ENTITY) {
            return;
        }
        QuantumTankState* s = tank_state(facts, self);
        if (!s || s->chassis.turret == WZ_INVALID_BEHAVIOR_ENTITY) {
            return;
        }
        const WzBehaviorEntityId target = args[0].entity;
        WzBehaviorEvent ev = self_event(self);

        const float yaw = tank_drive::clampf(
            tank_drive::face_bearing_to(facts, &ev, target),
            -tank_drive::kTurretHalfArc, tank_drive::kTurretHalfArc);
        tank_drive::aim_turret(facts, s->chassis.turret, yaw);
        s->chassis.turret_yaw = yaw;   // keep aim_error consistent with the turret

        if (s->barrel != WZ_INVALID_BEHAVIOR_ENTITY) {
            const float pitch = tank_drive::clampf(
                tank_drive::local_elevation_to(
                    facts, s->chassis.turret, s->barrel, target, kAimHeight),
                kGunElevationMin, kGunElevationMax);
            tank_drive::elevate_gun(facts, s->barrel, pitch);
        }
    }

    // fire_cannon(): discharge IF the tank has a shot -- gated on the SAME sense the C++
    // dispatch uses (`can_hit`, stashed by the mind each frame) plus ammo + reload
    // cooldown, so a chart calling this every frame in a FIRE state fires at most once per
    // kFireCooldown and only on a good solution (aim/range/LOS/gun/terrain). Its per-frame
    // advance (flight, flash) stays in the module tick -- this sets pending + rearms the
    // cooldown. The tactical WHEN (weapons-free vs conserve) is the chart's guard.
    void fire_cannon(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self,
        const WzActuatorArg*, uint32_t, void*)
    {
        QuantumTankState* s = tank_state(facts, self);
        if (!s || !s->can_hit || s->ammo == 0
            || s->canon_audio == WZ_INVALID_BEHAVIOR_ENTITY)
        {
            return;
        }
        const double now = wz_sim_time(facts);
        if (now < s->next_fire_time) {
            return;   // reload cooldown
        }
        cannon_fire::fire(&s->cannon);
        s->ammo--;
        s->next_fire_time = now + agent_tank_config::kFireCooldown;
    }

    // blink(): trigger the teleport. Its animation advance stays in the module tick.
    void blink(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self,
        const WzActuatorArg*, uint32_t, void*)
    {
        if (QuantumTankState* s = tank_state(facts, self)) {
            teleport::trigger(facts, &s->teleport);
        }
    }

    // request_reinforcement(): post one deploy request to the shared squad queue; the
    // pool consumes it at its own pace. Called from the COMMANDER node's chart. The
    // policy (when / how often) is the chart's; the actuator just posts.
    void request_reinforcement(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId,
        const WzActuatorArg*, uint32_t, void*)
    {
        void* raw = (facts && facts->find_shared_state)
            ? facts->find_shared_state(
                  facts->behavior_state_user, squad_deploy::kKey)
            : nullptr;
        if (raw) {
            static_cast<squad_deploy::Queue*>(raw)->pending += 1;
        }
    }

    // reanneal(): (COMMANDER) request the C++ commander to re-anneal its group agent --
    // reward doctrine + set the order/reinforce goals + rearm. Called from the commander
    // chart's order loop (Holding -> Deliberating). The reward/goal MATH stays in the
    // tank_commander module; this only sets the request flag the module consumes.
    void reanneal(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId self,
        const WzActuatorArg*, uint32_t, void*)
    {
        if (QuantumTankState* s = wz_instance_state_of<QuantumTankState>(
                facts, self, "tank_commander"))
        {
            s->reanneal_requested = 1;
        }
    }

    struct ActuatorDef
    {
        const char* name;
        const char* label;
        const WzActuatorParamDesc* params;
        uint32_t param_count;
        WzBehaviorActuatorFn fn;
    };

    constexpr WzActuatorParamDesc kPursueParams[] = {
        { "target", WZ_ACTUATOR_PARAM_BINDING, 0.0 },
        { "speed", WZ_ACTUATOR_PARAM_SCALAR, 6.0 },
    };
    constexpr WzActuatorParamDesc kOrbitParams[] = {
        { "target", WZ_ACTUATOR_PARAM_BINDING, 0.0 },
        { "standoff", WZ_ACTUATOR_PARAM_SCALAR, 30.0 },
        { "speed", WZ_ACTUATOR_PARAM_SCALAR, 6.0 },
    };
    constexpr WzActuatorParamDesc kTargetOnly[] = {
        { "target", WZ_ACTUATOR_PARAM_BINDING, 0.0 },
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

    const ActuatorDef defs[] = {
        { "pursue", "Pursue target", kPursueParams, 2u, pursue },
        { "orbit", "Orbit target", kOrbitParams, 3u, orbit },
        { "aim_at", "Aim at target", kTargetOnly, 1u, aim_at },
        { "halt", "Halt", nullptr, 0u, halt },
        { "fire_cannon", "Fire cannon", nullptr, 0u, fire_cannon },
        { "blink", "Blink", nullptr, 0u, blink },
        { "request_reinforcement", "Request reinforcement", nullptr, 0u,
          request_reinforcement },
        { "reanneal", "Re-anneal group (commander)", nullptr, 0u, reanneal },
    };

    for (const ActuatorDef& d : defs) {
        WzBehaviorActuatorDesc desc{};
        desc.size = sizeof(desc);
        desc.name = d.name;
        desc.label = d.label;
        desc.params = d.params;
        desc.param_count = d.param_count;
        desc.fn = d.fn;
        api->register_actuator(api->user, &desc);
    }
    return 1;
}
