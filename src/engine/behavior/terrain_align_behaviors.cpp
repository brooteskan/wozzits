#include <engine/behavior/terrain_align_behaviors.h>

#include <engine/behavior/behavior_module_api.h>

namespace wz::engine::behavior
{
    namespace
    {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

        // On WZ_EVENT_SELF_START (once, when the actor first materializes): read
        // the authored alignment rate (deg/sec) and push it onto THIS entity's
        // MotionComponent as radians/sec. The terrain-constraint pass consumes it
        // to rate-limit the alignment swing. Fired exactly once per binding -- the
        // host preserves the runtime rate across rebuilds, so it survives spawns
        // without per-frame re-assertion.
        void terrain_align_on_event(
            const WzBehaviorFrameFacts* facts,
            const WzBehaviorEvent* event,
            void*)
        {
            if (!facts || !event
                || event->kind != WZ_EVENT_SELF_START)
            {
                return;
            }

            float rate_deg = kTerrainAlignDefaultRateDeg;
            (void)wz_config_float(facts, kTerrainAlignRateConfigKey, &rate_deg);

            (void)wz_self_set_terrain_alignment_rate(
                facts, event, rate_deg * kDegToRad);
        }
    }

    uint8_t register_terrain_align_behaviors(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_module_desc)
        {
            return 0;
        }

        const char* channels[] = { "self.start" };
        const WzBehaviorParamDesc params[] = {
            { kTerrainAlignRateConfigKey, "Alignment rate (deg/s)",
                WZ_BEHAVIOR_PARAM_FLOAT, kTerrainAlignDefaultRateDeg, nullptr },
        };
        WzBehaviorModuleDesc desc{};
        desc.size = sizeof(desc);
        desc.module = kTerrainAlignModule;
        desc.on_event = terrain_align_on_event;
        desc.on_init = nullptr;
        desc.event_channels = channels;
        desc.event_channel_count = 1u;
        desc.module_user_data = nullptr;
        desc.params = params;
        desc.param_count = static_cast<uint32_t>(
            sizeof(params) / sizeof(params[0]));
        return api->register_module_desc(api->user, &desc);
    }
}
