#pragma once

// file: gpu/dx12/dx12.h
#include <gpu/gpu.h>

#include <render/frame/render_frame.h>

#include <gpu/dx12/dx12_mesh_wireframe_debug.h>

// Resolver and pipeline-cache types are only taken by reference in the
// submit_render_frame declarations below; callers that pass them already
// own the full engine/rendering includes.
namespace wz::engine::rendering
{
    class RenderResourceResolver;
    class RenderablePipelineCache;
    class RenderProgramPipelineCache;
}

namespace wz::gpu::dx12
{
    wz::gpu::Device create_device(void* native_window);

    void destroy_device(wz::gpu::Device& device);
    wz::gpu::DeviceStatus device_status(const wz::gpu::Device& device);
    const wz::gpu::DeviceLostInfo* device_lost_info(
        const wz::gpu::Device& device);
    bool resize(wz::gpu::Device& device, int w, int h);
    void wait_idle(wz::gpu::Device& device);

    uint64_t frame_timeline_value(wz::gpu::Device& device);
    uint64_t completed_timeline_value(wz::gpu::Device& device);

    bool begin_frame(wz::gpu::Device& device);
    void clear(wz::gpu::Device& device, float r, float g, float b, float a);


    // ── Triangle test path ───────────────────────────────────────────────

    void submit_triangle_test_frame(wz::gpu::Device& d);

    struct TriangleTestContextDesc
    {
        wz::gpu::GPUHandle vertex_shader{};
        wz::gpu::GPUHandle pixel_shader{};

        bool valid() const noexcept
        {
            return vertex_shader.valid() && pixel_shader.valid();
        }
    };

    void create_debug_triangle_opaque_context(
        wz::gpu::Device& device,
        const TriangleTestContextDesc& desc
    );

    // ── Debug opaque object path ─────────────────────────────────────────


    // ── Scalar field debug path ──────────────────────────────────────────

    struct ScalarFieldDebugView
    {
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        float zoom = 1.0f;
        float pad0 = 0.0f;

        float display_min = 0.0f;
        float display_max = 1.0f;
        bool normalize_for_display = true;
    };

    struct ScalarFieldDebugContextDesc
    {
        wz::gpu::GPUHandle vertex_shader{};
        wz::gpu::GPUHandle pixel_shader{};
        wz::gpu::GPUHandle scalar_field_texture{};

        float display_min = 0.0f;
        float display_max = 1.0f;
        bool normalize_for_display = true;

        bool valid() const noexcept
        {
            return vertex_shader.valid()
                && pixel_shader.valid()
                && scalar_field_texture.valid();
        }
    };

    void create_scalar_field_debug_context(
        wz::gpu::Device& device,
        const ScalarFieldDebugContextDesc& desc
    );

    void submit_scalar_field_debug_frame(
        wz::gpu::Device& device,
        const ScalarFieldDebugView& view
    );

    bool end_frame(wz::gpu::Device& device);
    bool present(wz::gpu::Device& device);
    bool present(wz::gpu::Device& device, uint32_t sync_interval);
}
