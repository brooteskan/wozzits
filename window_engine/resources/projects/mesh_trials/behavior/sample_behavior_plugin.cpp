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
        uint32_t frame_count = 0u;
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

    void submit_landscape_field(
        const WzBehaviorFrameFacts* facts,
        GpuTrialState* state)
    {
        if (!state) {
            return;
        }

        WzGpuJob job{};
        if (!wz_gpu_begin(&job, "project/landscape_field")) {
            return;
        }

        // The engine resolves everything mesh-shaped: vertex positions are
        // bound at t0, the output buffer is sized to the target field's
        // element count, the vertex-count constant is filled from the mesh,
        // and the dispatch group count is derived from the kernel's thread
        // group size. No vertex counts are authored anywhere.
        wz_gpu_set_groups_from_mesh(&job);
        wz_gpu_set_request_tag(&job, 2001u);

        wz_gpu_set_structured_input_mesh_positions(&job, "positions");

        // Output: per-vertex float, published as mesh field visualization.
        // Channel 0 tells the executor to use the target's default
        // channel_id (field_visualization_channel_id from mesh_render_style);
        // element count 0 lets the engine size the buffer.
        wz_gpu_set_structured_output_mesh_field(&job, "output", 0u, 0u);

        // Root constants: vertex_count (u32, engine-filled), phase (f32),
        // frequency (f32), bias (f32).
        wz_gpu_set_u32_mesh_vertex_count(&job, "vertex_count");

        const float phase =
            facts->timing
                ? static_cast<float>(facts->timing->elapsed_seconds) * 0.8f
                : 0.0f;
        wz_gpu_set_f32(&job, "phase", phase);
        wz_gpu_set_f32(&job, "frequency", 47.0f);
        wz_gpu_set_f32(&job, "bias", 0.5f);

        if (wz_gpu_submit(facts, &job, &state->work)) {
            state->submitted = 1u;
            state->completed = 0u;
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
            auto* state = static_cast<GpuTrialState*>(
                wz_get_instance_state(facts));
            submit_landscape_field(facts, state);
            break;
        }

        case WZ_EVENT_GPU_COMPUTE_COMPLETED: {
            auto* state = static_cast<GpuTrialState*>(
                wz_get_instance_state(facts));
            if (state) {
                state->completed = 1u;
                state->submitted = 0u;
                state->frame_count++;

                if (state->frame_count % 120u == 0u) {
                    wz_log_infof(
                        facts,
                        "landscape_field: %u dispatches completed",
                        state->frame_count);
                }

                // Re-submit for the next frame to animate the field.
                submit_landscape_field(facts, state);
            }
            break;
        }

        case WZ_EVENT_GPU_COMPUTE_FAILED: {
            wz_log_info(facts, "landscape_field: dispatch failed");
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

    struct WzFloat3 { float x, y, z; };

    WZ_GPU_KERNEL(landscape_kernel, "project/landscape_field")
    WZ_GPU_STRUCTURED_INPUT(landscape_kernel, "positions", WzFloat3);
    WZ_GPU_STRUCTURED_OUTPUT(landscape_kernel, "output", float);
    WZ_GPU_U32(landscape_kernel, "vertex_count");
    WZ_GPU_F32(landscape_kernel, "phase");
    WZ_GPU_F32(landscape_kernel, "frequency");
    WZ_GPU_F32(landscape_kernel, "bias");
    WZ_GPU_KERNEL_END(api, landscape_kernel);

    return 1u;
}
