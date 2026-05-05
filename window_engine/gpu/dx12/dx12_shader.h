#pragma once
// gpu/dx12/dx12_shader.h

#include <gpu/gpu_types.h>
#include <gpu/shader.h>

#include <d3dcompiler.h>
#include <cstdint>
#include <vector>

namespace wz::gpu::dx12
{
    struct DX12Shader
    {
        ID3DBlob* blob = nullptr;
        wz::gpu::ShaderStage stage{};
    };

    struct DX12ShaderSlot
    {
        DX12Shader shader{};
        uint32_t epoch = 1;
        bool occupied = false;
    };

    struct DX12ShaderTable
    {
        std::vector<DX12ShaderSlot> slots;

        wz::gpu::GPUHandle add(ID3DBlob* blob, wz::gpu::ShaderStage stage)
        {
            DX12ShaderSlot slot{};
            slot.shader.blob = blob;
            slot.shader.stage = stage;
            slot.epoch = 1;
            slot.occupied = true;

            slots.push_back(slot);

            return wz::gpu::GPUHandle{
                .id = static_cast<uint32_t>(slots.size()), // 1-based
                .epoch = slot.epoch,
                .type = wz::gpu::GPUResourceType::Shader
            };
        }

        DX12Shader* get(wz::gpu::GPUHandle handle)
        {
            if (!handle.valid())
                return nullptr;

            if (handle.type != wz::gpu::GPUResourceType::Shader)
                return nullptr;

            const uint32_t index = handle.id - 1;
            if (index >= slots.size())
                return nullptr;

            DX12ShaderSlot& slot = slots[index];

            if (!slot.occupied)
                return nullptr;

            if (slot.epoch != handle.epoch)
                return nullptr;

            return &slot.shader;
        }

        void destroy()
        {
            for (DX12ShaderSlot& slot : slots)
            {
                if (slot.shader.blob)
                {
                    slot.shader.blob->Release();
                    slot.shader.blob = nullptr;
                }

                slot.occupied = false;
            }

            slots.clear();
        }
    };
}