// file: src/engine/render_backends/dx12/dx12_submit.cpp

#include <d3d12.h>

#include <engine/render_backends/dx12/dx12_submit.h>
#include <gpu/dx12/dx12_internal.h>


namespace wz::render::backend::dx12
{
    struct Context
    {
        wz::gpu::Device* device;

        // DX12 objects
        ID3D12RootSignature* root_sig = nullptr;
        ID3D12PipelineState* pso = nullptr;

        ID3D12Resource* vertex_buffer = nullptr;
        D3D12_VERTEX_BUFFER_VIEW vb_view{};

        UINT vertex_count = 3;
    };

    Context* create(wz::gpu::Device& device)
    {
        auto* dev = wz::gpu::dx12::internal::get_device(device);
        Context* ctx = new Context();
        ctx->device = &device;

        return ctx;
    }

    void submit(Context* ctx, const RenderFrame& frame)
    {
        auto* cmd = wz::gpu::dx12::internal::get_command_list(*ctx->device);

        // later:
        // bind PSO
        // bind root signature
        // bind VB
        // draw
    }
}