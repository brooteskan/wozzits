// src/gpu/dx12/dx12_internal.cpp

#include <gpu/dx12/dx12_internal.h>
#include <gpu/dx12/dx12_pipeline_factory.h>
#include "dx12_device_internal.h"

namespace wz::gpu::dx12::internal {
    namespace
    {
        void release_mesh_resource(DX12MeshResource& mesh)
        {
            if (mesh.vertex_buffer) {
                mesh.vertex_buffer->Release();
                mesh.vertex_buffer = nullptr;
            }

            if (mesh.index_buffer) {
                mesh.index_buffer->Release();
                mesh.index_buffer = nullptr;
            }

            if (mesh.vertex_upload) {
                mesh.vertex_upload->Release();
                mesh.vertex_upload = nullptr;
            }

            if (mesh.index_upload) {
                mesh.index_upload->Release();
                mesh.index_upload = nullptr;
            }

            mesh.vertex_view = {};
            mesh.index_view = {};
            mesh.vertex_count = 0;
            mesh.index_count = 0;
        }
    }

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

    const DX12ScalarFieldTexture* get_scalar_field_texture(
        Device& device,
        GPUHandle handle)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        if (!impl)
            return nullptr;
        return impl->scalar_field_textures.get(handle);
    }

    ID3D12DescriptorHeap* get_scalar_field_srv_heap(Device& device)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        return impl ? impl->scalar_field_srv_heap : nullptr;
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

            release_mesh_resource(slot.mesh);
            slot.occupied = false;
            ++slot.epoch;
        }

        slots_.clear();
        slots_.emplace_back();
    }

    GPUHandle DX12MeshTable::add(DX12MeshResource mesh)
    {
        for (uint32_t id = 1; id < static_cast<uint32_t>(slots_.size()); ++id)
        {
            Slot& slot = slots_[id];
            if (slot.occupied)
                continue;

            if (slot.epoch == 0)
                slot.epoch = 1;
            slot.occupied = true;
            slot.mesh = mesh;

            return GPUHandle{
                .id = id,
                .epoch = slot.epoch,
                .type = GPUResourceType::Mesh,
            };
        }

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

    bool DX12MeshTable::release(GPUHandle handle)
    {
        if (!handle.valid())
            return false;

        if (handle.type != GPUResourceType::Mesh)
            return false;

        if (handle.id == 0)
            return false;

        if (handle.id >= slots_.size())
            return false;

        Slot& slot = slots_[handle.id];

        if (!slot.occupied)
            return false;

        if (slot.epoch != handle.epoch)
            return false;

        release_mesh_resource(slot.mesh);
        slot.occupied = false;
        ++slot.epoch;
        if (slot.epoch == 0)
            slot.epoch = 1;
        return true;
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

    GPUHandle create_graphics_pipeline_from_data(
        Device& device,
        const wz::engine::assets::RenderProgramData& data,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        if (!impl)
            return {};

        ID3D12RootSignature* root_sig =
            create_root_signature_from_data(impl->device, data);
        if (!root_sig)
            return {};

        ID3D12PipelineState* pso =
            create_pso_from_data(device, data, root_sig, vertex_shader, pixel_shader);
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
