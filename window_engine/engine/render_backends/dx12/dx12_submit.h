#pragma once
// file: engine/render_backends/dx12/dx12_submit.h



#include <render/frame/render_frame.h>
#include <gpu/gpu.h>

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

    Context* create(wz::gpu::Device& device);

    void destroy(Context* ctx);

    // THIS is the important function
    void submit(Context* ctx, const RenderFrame& frame);
}