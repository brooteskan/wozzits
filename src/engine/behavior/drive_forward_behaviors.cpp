#include <engine/behavior/drive_forward_behaviors.h>

#include <engine/behavior/behavior_module_api.h>

namespace wz::engine::behavior
{
    namespace
    {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

        // Per-frame: drive the entity along its OWN local forward (+Z) at the
        // configured speed by setting its motion velocity in LOCAL space. We do not
        // touch the transform directly — setting velocity feeds the shared motion
        // integrator, which advances the node along local forward in world axes and
        // (for a terrain-constrained node) lets the terrain pass re-ground Y while
        // preserving the horizontal step. Local space is the key choice: the
        // direction follows the node's own rotation, so the tank drives where it is
        // facing with no reference to any other node — clean under spawn
        // id-remapping. An optional yaw (turn_rate) about local +Y makes it roam.
        void drive_forward(
            const WzBehaviorFrameFacts* facts,
            WzBehaviorEntityId entity,
            void*)
        {
            if (!facts || !facts->write_command) {
                return;
            }

            float speed = kDriveForwardDefaultSpeed;
            (void)wz_config_float(facts, kDriveForwardSpeedConfigKey, &speed);

            float turn_rate_deg = 0.0f;
            (void)wz_config_float(
                facts, kDriveForwardTurnRateConfigKey, &turn_rate_deg);

            // Drive along local +Z; the integrator rotates this into world axes.
            (void)wz_write_set_motion_space(
                facts, entity, WZ_BEHAVIOR_MOTION_SPACE_LOCAL);
            (void)wz_write_set_linear_velocity(facts, entity, 0.0f, 0.0f, speed);

            // Yaw about local +Y so it gently turns rather than running straight to
            // infinity. radians/sec; 0 leaves it driving dead ahead.
            (void)wz_write_set_angular_velocity(
                facts, entity, 0.0f, turn_rate_deg * kDegToRad, 0.0f);
        }
    }

    uint8_t register_drive_forward_behaviors(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_behavior)
        {
            return 0;
        }

        return api->register_behavior(
            api->user,
            kDriveForwardModule,
            kDriveForwardBehavior,
            drive_forward,
            nullptr);
    }
}
