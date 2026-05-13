#pragma once

// file: gpu/dx12/dx12_internal.h

#include <vector>
#include <gpu/gpu.h>
#include <gpu/dx12/external/d3dx12.h>
#include <gpu/gpu_types.h>
#include <gpu/shader.h>
#include <d3dcompiler.h>
#include <d3d12.h>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

namespace wz::gpu::dx12
{
    struct DX12Shader;
}

namespace wz::gpu::dx12::internal
{
    ID3D12Device* get_device(Device& d);
    ID3D12GraphicsCommandList* get_command_list(Device& d);
    ID3D12RootSignature* create_empty_root_signature(ID3D12Device* device);

    // added for imgui tooling integration.
    DXGI_FORMAT get_backbuffer_format();
    UINT get_backbuffer_count();
    ID3D12CommandQueue* get_command_queue(Device& d);
    D3D12_CPU_DESCRIPTOR_HANDLE get_current_rtv(Device& d);
    UINT get_width(Device& d);
    UINT get_height(Device& d);

    ID3D12PipelineState* create_triangle_pso(
        wz::gpu::Device& device,
        ID3D12RootSignature* root_sig,
        wz::gpu::GPUHandle vertex_shader,
        wz::gpu::GPUHandle pixel_shader
    );

    wz::gpu::GPUHandle store_shader(
        wz::gpu::Device& device,
        ID3DBlob* blob,
        wz::gpu::ShaderStage stage
    );

    const wz::gpu::dx12::DX12Shader* get_shader(
        wz::gpu::Device& device,
        wz::gpu::GPUHandle handle
    );
    
}

// ── Scalar field texture ─────────────────────────────────────────
namespace wz::engine::assets {
    struct ScalarFieldData;
}

namespace wz::gpu::dx12::internal {

    struct DX12ScalarFieldTexture
    {
        ID3D12Resource* texture = nullptr;

        D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu{};

        uint32_t width = 0;
        uint32_t height = 0;
    };

    class DX12ScalarFieldTextureTable
    {
    public:
        DX12ScalarFieldTextureTable();

        GPUHandle add(DX12ScalarFieldTexture tex);
        const DX12ScalarFieldTexture* get(GPUHandle handle) const;
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            DX12ScalarFieldTexture tex{};
        };

        std::vector<Slot> slots_;
    };

    GPUHandle upload_scalar_field_texture_dx12(
        Device& device,
        const wz::engine::assets::ScalarFieldData& field
    );
}



// ── Mesh buffers ──────────────────────────────────────────────────

namespace wz::engine::assets {
    struct ScalarFieldData;
    struct MeshData;
}

namespace wz::gpu::dx12::internal {

    struct DX12MeshResource
    {
        ID3D12Resource* vertex_buffer = nullptr;
        ID3D12Resource* index_buffer = nullptr;

        // Kept alive for V1 until we have explicit upload/fence lifetime handling.
        ID3D12Resource* vertex_upload = nullptr;
        ID3D12Resource* index_upload = nullptr;

        D3D12_VERTEX_BUFFER_VIEW vertex_view{};
        D3D12_INDEX_BUFFER_VIEW  index_view{};

        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
    };

    class DX12MeshTable
    {
    public:
        DX12MeshTable();

        GPUHandle add(DX12MeshResource mesh);
        const DX12MeshResource* get(GPUHandle handle) const;
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            DX12MeshResource mesh{};
        };

        std::vector<Slot> slots_;
    };

    GPUHandle upload_mesh_dx12(
        Device& device,
        const wz::engine::assets::MeshData& mesh
    );

    const DX12MeshResource* get_mesh(
        Device& device,
        GPUHandle handle
    );
}