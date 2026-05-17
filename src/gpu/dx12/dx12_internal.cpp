// src/gpu/dx12/dx12_internal.cpp

#include <gpu/dx12/dx12_internal.h>
#include "dx12_device_internal.h"

namespace wz::gpu::dx12::internal {

    GaussianSplatDebugPipelineRef get_gaussian_splat_debug_pipeline(Device& d)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(d.impl);
        if (!impl || !impl->gaussian_splat_debug_ctx)
            return {};
        return {
            .root_sig = impl->gaussian_splat_debug_ctx->root_sig,
            .pso      = impl->gaussian_splat_debug_ctx->pso,
        };
    }

    MeshWireframePipelineRef get_mesh_wireframe_pipeline(Device& d)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(d.impl);
        if (!impl || !impl->mesh_wire_debug_ctx)
            return {};
        return {
            .root_sig = impl->mesh_wire_debug_ctx->root_sig,
            .pso      = impl->mesh_wire_debug_ctx->pso,
        };
    }





	DX12ScalarFieldTextureTable::DX12ScalarFieldTextureTable()
	{
		slots_.emplace_back(); // id 0 invalid
	}

	void DX12ScalarFieldTextureTable::destroy()
	{
		for (Slot& slot : slots_) {
			if (slot.occupied && slot.tex.texture) {
				slot.tex.texture->Release();
				slot.tex.texture = nullptr;
			}
		}

		slots_.clear();
		slots_.emplace_back();
	}

    GPUHandle DX12ScalarFieldTextureTable::add(DX12ScalarFieldTexture tex)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.tex = tex;

        slots_.push_back(slot);

        return GPUHandle{
            .id = static_cast<uint32_t>(slots_.size() - 1), // because slot 0 is sentinel
            .epoch = slot.epoch,
            .type = GPUResourceType::Texture,
        };
    }

    const DX12ScalarFieldTexture* DX12ScalarFieldTextureTable::get(
        GPUHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != GPUResourceType::Texture)
            return nullptr;

        if (handle.id == 0)
            return nullptr;

        if (handle.id >= slots_.size())
            return nullptr;

        const Slot& slot = slots_[handle.id];

        if (!slot.occupied)
            return nullptr;

        if (slot.epoch != handle.epoch)
            return nullptr;

        return &slot.tex;
    }

    DX12MeshTable::DX12MeshTable()
    {
        slots_.emplace_back(); // id 0 invalid
    }

    void DX12MeshTable::destroy()
    {
        for (Slot& slot : slots_) {
            if (!slot.occupied)
                continue;

            if (slot.mesh.vertex_buffer) {
                slot.mesh.vertex_buffer->Release();
                slot.mesh.vertex_buffer = nullptr;
            }

            if (slot.mesh.index_buffer) {
                slot.mesh.index_buffer->Release();
                slot.mesh.index_buffer = nullptr;
            }

            if (slot.mesh.vertex_upload) {
                slot.mesh.vertex_upload->Release();
                slot.mesh.vertex_upload = nullptr;
            }

            if (slot.mesh.index_upload) {
                slot.mesh.index_upload->Release();
                slot.mesh.index_upload = nullptr;
            }
        }

        slots_.clear();
        slots_.emplace_back();
    }

    GPUHandle DX12MeshTable::add(DX12MeshResource mesh)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.mesh = mesh;

        slots_.push_back(slot);

        return GPUHandle{
            .id = static_cast<uint32_t>(slots_.size() - 1),
            .epoch = slot.epoch,
            .type = GPUResourceType::Mesh,
        };
    }

    const DX12MeshResource* DX12MeshTable::get(GPUHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != GPUResourceType::Mesh)
            return nullptr;

        if (handle.id == 0)
            return nullptr;

        if (handle.id >= slots_.size())
            return nullptr;

        const Slot& slot = slots_[handle.id];

        if (!slot.occupied)
            return nullptr;

        if (slot.epoch != handle.epoch)
            return nullptr;

        return &slot.mesh;
    }
}