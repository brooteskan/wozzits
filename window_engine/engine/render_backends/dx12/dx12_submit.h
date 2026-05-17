#pragma once
// file: engine/render_backends/dx12/dx12_submit.h

#include <render/frame/render_frame.h>
#include <gpu/gpu.h>
#include <engine/rendering/render_resource_resolver.h>

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

    // Resolver overload: draws splat DrawCommands via RenderResourceResolver.
    // Does not require an opaque Context — takes Device& directly so it can
    // be used by pure-splat toolhosts that never call create_debug_opaque_context.
    // Opaque DrawCommands in frame.opaque are silently skipped; mesh-resolver
    // support will be added when MeshHandle is wired to the resolver.
    void submit(wz::gpu::Device& device,
                const RenderFrameView& frame,
                const wz::engine::rendering::RenderResourceResolver& resolver);
}