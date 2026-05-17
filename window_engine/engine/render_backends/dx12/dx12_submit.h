#pragma once
// file: engine/render_backends/dx12/dx12_submit.h

#include <render/frame/render_frame.h>
#include <gpu/gpu.h>
#include <engine/rendering/render_resource_resolver.h>
#include <engine/rendering/renderable_pipeline_cache.h>

namespace wz::render::backend::dx12
{
    struct GpuMesh
    {
        // Non-owning in the current triangle test path.
        // The actual resource is owned by Context::vertex_buffer.
        ID3D12Resource* vertex_buffer = nullptr;
        ID3D12Resource* index_buffer = nullptr;

        D3D12_VERTEX_BUFFER_VIEW vb_view{};
        D3D12_INDEX_BUFFER_VIEW ib_view{};

        uint32_t index_count = 0;
    };

    struct Context
    {
        wz::gpu::Device* device = nullptr;

        ID3D12RootSignature* root_sig = nullptr;
        ID3D12PipelineState* pso = nullptr;

        ID3D12Resource* vertex_buffer = nullptr;
        D3D12_VERTEX_BUFFER_VIEW vb_view{};

        UINT vertex_count = 3;

        std::vector<GpuMesh> mesh_table;

    };

    struct TrianglePipelineDesc
    {
        wz::gpu::GPUHandle vertex_shader{};
        wz::gpu::GPUHandle pixel_shader{};

        bool valid() const noexcept
        {
            return vertex_shader.valid() && pixel_shader.valid();
        }
    };

    Context* create(
        wz::gpu::Device& device,
        const TrianglePipelineDesc& desc
    );

    void destroy(Context* ctx);

    // THIS is the important function
    void submit(Context* ctx, const RenderFrameView& frame);

    // Transitional resolver overload: uses legacy device-singleton debug pipelines.
    // Prefer the overload that takes RenderablePipelineCache.
    void submit(wz::gpu::Device& device,
                const RenderFrameView& frame,
                const wz::engine::rendering::RenderResourceResolver& resolver);

    // Production resolver overload: resolves both GPU resources and pipelines
    // through the supplied caches.  Does not depend on any device-singleton.
    void submit(wz::gpu::Device& device,
                const RenderFrameView& frame,
                const wz::engine::rendering::RenderResourceResolver& resolver,
                const wz::engine::rendering::RenderablePipelineCache& pipeline_cache);
}