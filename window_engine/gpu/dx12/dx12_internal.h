#pragma once

// file: gpu/dx12/dx12_internal.h


#include <gpu/gpu.h>
#include <gpu/dx12/external/d3dx12.h>
#include <gpu/gpu_types.h>
#include <gpu/shader.h>
#include <d3dcompiler.h>

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