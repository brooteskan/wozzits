#pragma once

// file: gpu/dx12/dx12.h
#include <gpu/gpu.h>

#include <render/frame/render_frame.h>

#include <gpu/dx12/dx12_mesh_wireframe_debug.h>

namespace wz::gpu::dx12
{
    wz::gpu::Device create_device(void* native_window);

    void destroy_device(wz::gpu::Device& device);
    void resize(wz::gpu::Device& device, int w, int h);

    void begin_frame(wz::gpu::Device& device);
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


    struct DebugOpaqueContextDesc
    {
        wz::gpu::GPUHandle vertex_shader{};
        wz::gpu::GPUHandle pixel_shader{};

        bool valid() const noexcept
        {
            return vertex_shader.valid() && pixel_shader.valid();
        }
    };

    void create_debug_opaque_context(
        wz::gpu::Device& device,
        const DebugOpaqueContextDesc& desc
    );

    void submit_render_frame(
        wz::gpu::Device& device,
        const wz::render::RenderFrameView& frame
    );

    // ── Scalar field debug path ──────────────────────────────────────────

    struct ScalarFieldDebugView
    {
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        float zoom = 1.0f;
        float pad0 = 0.0f;
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

    void end_frame(wz::gpu::Device& device);
    void present(wz::gpu::Device& device);
}