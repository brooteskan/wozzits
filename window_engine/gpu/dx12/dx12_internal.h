#pragma once

// file: gpu/dx12/dx12_internal.h


#include <gpu/gpu.h>
#include <gpu/dx12/external/d3dx12.h>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

namespace wz::gpu::dx12::internal
{
    ID3D12Device* get_device(Device& d);
    ID3D12GraphicsCommandList* get_command_list(Device& d);
    ID3D12RootSignature* create_empty_root_signature(ID3D12Device* device);
    ID3D12PipelineState* create_triangle_pso(
        ID3D12Device* device,
        ID3D12RootSignature* root_sig);
}