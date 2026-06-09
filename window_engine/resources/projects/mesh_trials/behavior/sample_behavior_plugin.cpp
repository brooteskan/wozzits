#include <engine/behavior/behavior_module_api.h>

#include <cstdio>

namespace
{
    static const char* kEvents[] = {
        "gpu.compute.*"
    };

    struct GpuTrialState {
        uint8_t submitted = 0;
        uint8_t completed = 0;
        WzGpuWorkId work{};
    };

    void gpu_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*
    )
    {
        wz_log_info(facts,"behavior: init");
        auto* state = static_cast<GpuTrialState*>(
            wz_alloc_instance_state(
                facts,
                sizeof(GpuTrialState),
                alignof(GpuTrialState)));

        if (state) {
            *state = {};
        }
    }

    void on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }

        switch (wz_event_kind(event)) {
        case WZ_EVENT_GPU_COMPUTE_REQUEST: {
            auto* state = static_cast<GpuTrialState*>(wz_get_instance_state(facts));


            const uint32_t input[] = { 1u, 2u, 3u, 4u };

            WzGpuJob job{};
            if (!wz_gpu_begin(&job, "debug/multiply_u32")) {
                break;
            }

            wz_gpu_set_groups(&job, 1u, 1u, 1u);
            wz_gpu_set_request_tag(&job, 1001u);
            wz_gpu_set_structured_input(&job, "input", 4u, sizeof(uint32_t), input, sizeof(input));
            wz_gpu_set_structured_output(&job, "output", 4u, sizeof(uint32_t));
            wz_gpu_set_u32(&job, "factor", 7u);
            wz_gpu_set_u32(&job, "count", 4u);

            if (wz_gpu_submit(facts, &job, &state->work)) {
                state->submitted = 1u;
                state->completed = 0u;
                wz_log_infof(facts, "gpu submitted work=%u", state->work.value);
            }
            break;
        }

        case WZ_EVENT_GPU_COMPUTE_COMPLETED: {
            auto* state = static_cast<GpuTrialState*>(wz_get_instance_state(facts));
            if (state && !state->completed) {
                uint32_t output[4]{};
                const uint8_t read_ok =
                    wz_gpu_compute_read_output_u32(facts, "output", 0u, &output[0])
                    && wz_gpu_compute_read_output_u32(facts, "output", 1u, &output[1])
                    && wz_gpu_compute_read_output_u32(facts, "output", 2u, &output[2])
                    && wz_gpu_compute_read_output_u32(facts, "output", 3u, &output[3]);
                wz_log_infof(
                    facts,
                    "gpu completed work=%u tag=%llu outputs=%u values=%s %u %u %u %u",
                    wz_gpu_compute_event_work(facts).value,
                    (unsigned long long)wz_gpu_compute_event_request_tag(facts),
                    wz_gpu_compute_event_output_count(facts),
                    read_ok ? "ok" : "missing",
                    output[0],
                    output[1],
                    output[2],
                    output[3]);
                state->completed = 1u;
                state->submitted = 0u;
            }

            break;
        }

        case WZ_EVENT_GPU_COMPUTE_FAILED: {
            auto* state = static_cast<GpuTrialState*>(wz_get_instance_state(facts));

            wz_log_info(facts,"behavior: WZ_EVENT_GPU_COMPUTE_FAILED");
            break;
        }
        default:
            break;
        }
    }
}

extern "C" WZ_BEHAVIOR_MODULE_EXPORT uint8_t wz_register_behaviors(
    WzBehaviorPluginApi* api)
{
    if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
        || !api->register_module_desc)
    {
        return 0u;
    }

    const WzBehaviorModuleDesc desc = {
        sizeof(WzBehaviorModuleDesc),
        "gpu_trial",
        on_event,
        gpu_init,
        kEvents,
        (uint32_t)(sizeof(kEvents) / sizeof(kEvents[0])),
        nullptr,
    };
    if (!api->register_module_desc(api->user, &desc)) {
        return 0u;
    }

    WZ_GPU_KERNEL(multiply_kernel, "debug/multiply_u32")
    WZ_GPU_STRUCTURED_INPUT(multiply_kernel, "input", uint32_t);
    WZ_GPU_STRUCTURED_OUTPUT(multiply_kernel, "output", uint32_t);
    WZ_GPU_U32(multiply_kernel, "factor");
    WZ_GPU_U32(multiply_kernel, "count");
    WZ_GPU_KERNEL_END(api, multiply_kernel);

    return 1u;
}
