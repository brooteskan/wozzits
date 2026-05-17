// src/gpu/dx12/dx12_internal.cpp

#include <gpu/dx12/dx12_internal.h>
#include <gpu/dx12/dx12_pipeline_factory.h>
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


    // ── DX12GraphicsPipelineTable ─────────────────────────────────────────────

    DX12GraphicsPipelineTable::DX12GraphicsPipelineTable()
    {
        slots_.emplace_back(); // slot 0 reserved as sentinel
    }

    GPUHandle DX12GraphicsPipelineTable::add(DX12GraphicsPipeline pipeline)
    {
        Slot slot{};
        slot.epoch    = 1;
        slot.occupied = true;
        slot.pipeline = pipeline;

        slots_.push_back(slot);

        return GPUHandle{
            .id    = static_cast<uint32_t>(slots_.size() - 1),
            .epoch = slot.epoch,
            .type  = wz::engine::assets::kAssetTypeGPUGraphicsPipeline,
        };
    }

    const DX12GraphicsPipeline* DX12GraphicsPipelineTable::get(GPUHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != wz::engine::assets::kAssetTypeGPUGraphicsPipeline)
            return nullptr;

        if (handle.id == 0 || handle.id >= slots_.size())
            return nullptr;

        const Slot& slot = slots_[handle.id];

        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        return &slot.pipeline;
    }

    void DX12GraphicsPipelineTable::destroy()
    {
        for (Slot& slot : slots_)
        {
            if (!slot.occupied)
                continue;

            if (slot.pipeline.pso)
            {
                slot.pipeline.pso->Release();
                slot.pipeline.pso = nullptr;
            }

            if (slot.pipeline.root_sig)
            {
                slot.pipeline.root_sig->Release();
                slot.pipeline.root_sig = nullptr;
            }
        }

        slots_.clear();
        slots_.emplace_back();
    }


    // ── create_graphics_pipeline / get_graphics_pipeline ─────────────────────

    GPUHandle create_graphics_pipeline(
        Device& device,
        wz::engine::assets::BuiltinRenderProgram program,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        if (!impl)
            return {};

        ID3D12RootSignature* root_sig =
            create_root_signature_for_program(impl->device, program);
        if (!root_sig)
            return {};

        ID3D12PipelineState* pso =
            create_pso_for_program(device, program, root_sig, vertex_shader, pixel_shader);
        if (!pso)
        {
            root_sig->Release();
            return {};
        }

        return impl->graphics_pipelines.add({ root_sig, pso });
    }

    const DX12GraphicsPipeline* get_graphics_pipeline(
        Device& device,
        GPUHandle handle)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        if (!impl)
            return nullptr;

        return impl->graphics_pipelines.get(handle);
    }
}