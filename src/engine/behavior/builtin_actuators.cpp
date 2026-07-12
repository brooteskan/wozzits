#include <engine/behavior/builtin_actuators.h>

#include <engine/behavior/behavior_module_api.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::behavior
{
    namespace
    {
        // move_toward(target: binding, speed: scalar) -- step `self` HORIZONTALLY
        // toward the bound target at `speed` world-units/second, clamped so it never
        // overshoots. Horizontal-only (x/z), matching the Proximity sensor, so a
        // locomotion actuator does not fight gravity/terrain on the y axis. This is
        // the first actuator that turns a statechart's decision into motion, and it
        // does it through the same facts a hand-written behavior uses -- no engine
        // special-casing, exactly what a project-registered actuator would do.
        void move_toward(
            const WzBehaviorFrameFacts* facts,
            WzBehaviorEntityId self,
            const WzActuatorArg* args,
            uint32_t arg_count,
            void*)
        {
            if (!facts || arg_count < 1
                || args[0].kind != WZ_ACTUATOR_ARG_ENTITY
                || args[0].entity == static_cast<WzBehaviorEntityId>(
                                         WZ_INVALID_BEHAVIOR_ENTITY))
            {
                return;
            }
            const WzBehaviorEntityId target = args[0].entity;
            const double speed =
                (arg_count >= 2 && args[1].kind == WZ_ACTUATOR_ARG_SCALAR)
                    ? args[1].scalar
                    : 1.0;

            WzVec3 me{};
            WzVec3 them{};
            if (!wz_read_world_position(facts, self, &me)
                || !wz_read_world_position(facts, target, &them))
            {
                return;
            }

            const double dx =
                static_cast<double>(them.x) - static_cast<double>(me.x);
            const double dz =
                static_cast<double>(them.z) - static_cast<double>(me.z);
            const double dist = std::sqrt(dx * dx + dz * dz);
            if (dist <= 1.0e-4) {
                return;   // already there (horizontally)
            }

            double step = speed * static_cast<double>(wz_delta_seconds(facts));
            if (step <= 0.0) {
                return;
            }
            if (step > dist) {
                step = dist;   // clamp: do not overshoot the target
            }
            const double scale = step / dist;
            wz_write_add_world_translation(
                facts,
                self,
                static_cast<float>(dx * scale),
                0.0f,
                static_cast<float>(dz * scale));
        }
    }

    uint8_t register_builtin_actuators(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_actuator)
        {
            return 0;
        }

        static const WzActuatorParamDesc move_toward_params[] = {
            { "target", WZ_ACTUATOR_PARAM_BINDING, 0.0 },
            { "speed", WZ_ACTUATOR_PARAM_SCALAR, 1.0 },
        };

        WzBehaviorActuatorDesc move_toward_desc{};
        move_toward_desc.size = sizeof(move_toward_desc);
        move_toward_desc.name = "move_toward";
        move_toward_desc.label = "Move toward";
        move_toward_desc.params = move_toward_params;
        move_toward_desc.param_count =
            static_cast<uint32_t>(
                sizeof(move_toward_params) / sizeof(move_toward_params[0]));
        move_toward_desc.fn = move_toward;
        move_toward_desc.user_data = nullptr;

        return api->register_actuator(api->user, &move_toward_desc);
    }
}
