#pragma once
// file: engine/render_backends/dx12/dx12_submit.h



#include <render/frame/render_frame.h>
#include <gpu/gpu.h>

namespace wz::render::backend::dx12
{
    struct GpuMesh
    {
        ID3D12Resource* vertex_buffer;
        ID3D12Resource* index_buffer;

        D3D12_VERTEX_BUFFER_VIEW vb_view;
        D3D12_INDEX_BUFFER_VIEW ib_view;

        uint32_t index_count;
    };

    struct Context
    {
        wz::gpu::Device* device;

        // DX12 objects
        ID3D12RootSignature* root_sig = nullptr;
        ID3D12PipelineState* pso = nullptr;

        ID3D12Resource* vertex_buffer = nullptr;
        D3D12_VERTEX_BUFFER_VIEW vb_view{};

        UINT vertex_count = 3;

        std::vector<GpuMesh> mesh_table;
    };

    Context* create(wz::gpu::Device& device);

    void destroy(Context* ctx);

    // THIS is the important function
    void submit(Context* ctx, const RenderFrame& frame);
}