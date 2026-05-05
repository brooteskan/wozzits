// src/gpu/dx12/dx12_shader.cpp

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <gpu/gpu.h>
#include <gpu/dx12/dx12_internal.h>
#include <engine/assets/shader/shader_types.h>

#include <d3dcompiler.h>

#include <span>
#include <string>
#include <vector>
#include <cstdint>

#pragma comment(lib, "d3dcompiler.lib")

namespace wz::gpu
{
    wz::asset::ResourceHandle compile_hlsl(
        wz::gpu::Device& device,
        std::span<const std::span<const uint8_t>> sources,
        const HLSLCompileDesc& desc)
    {
        // Not needed by D3DCompile itself, but this verifies the GPU device is valid
        // and keeps the function shaped correctly for the later blob table.
        ID3D12Device* d3d = wz::gpu::dx12::internal::get_device(device);
        if (!d3d)
            return INVALID_GPU_HANDLE;

        if (sources.empty())
            return INVALID_GPU_HANDLE;

        if (desc.primary_source_index >= sources.size())
            return INVALID_GPU_HANDLE;

        // Simple first pass: concatenate in declared dependency order.
        //
        // Later, if you want #include semantics, use D3DCompile include handlers
        // or DXC with a custom include provider. For now, this matches your asset
        // DAG model where source order is meaningful.
        std::string combined;

        for (std::span<const uint8_t> src : sources)
        {
            if (src.empty())
                continue;

            combined.append(
                reinterpret_cast<const char*>(src.data()),
                src.size()
            );

            combined.push_back('\n');
        }

        ID3DBlob* shader_blob = nullptr;
        ID3DBlob* error_blob = nullptr;

        HRESULT hr = D3DCompile(
            combined.data(),
            combined.size(),
            nullptr,
            nullptr,
            nullptr,
            desc.entry.c_str(),
            desc.target.c_str(),
            0,
            0,
            &shader_blob,
            &error_blob
        );

        if (FAILED(hr))
        {
            if (error_blob)
            {
                OutputDebugStringA(
                    static_cast<const char*>(error_blob->GetBufferPointer())
                );

                error_blob->Release();
                error_blob = nullptr;
            }

            if (shader_blob)
            {
                shader_blob->Release();
                shader_blob = nullptr;
            }

            return INVALID_GPU_HANDLE;
        }

        if (error_blob)
        {
            error_blob->Release();
            error_blob = nullptr;
        }

        return wz::gpu::dx12::internal::store_shader(
            device,
            shader_blob,
            desc.stage
        );
    }
}
