#include <engine/rendering/rhi_dx12_command_recorder.h>

#include <gpu/dx12/dx12_descriptor_allocator.h>
#include <gpu/dx12/dx12_internal.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>

namespace wz::engine::rendering
{
    struct RhiDx12CommandRecorder::DescriptorTableCache
    {
        struct Entry
        {
            uint32_t binding_slot = 0;
            std::vector<wz::gpu::GPUHandle> buffers;
            wz::gpu::dx12::DX12DescriptorTable table{};
        };

        std::vector<Entry> entries;
    };

    RhiDx12CommandRecorder::RhiDx12CommandRecorder(
        wz::gpu::Device& device,
        RhiDx12PipelineCache& pipelines,
        const wz::rhi::GpuResourceRegistry& resources,
        const EngineGpuBackend& backend)
        : device_(&device)
        , pipelines_(&pipelines)
        , resources_(&resources)
        , backend_(&backend)
        , descriptor_tables_(std::make_unique<DescriptorTableCache>())
    {
    }

    RhiDx12CommandRecorder::~RhiDx12CommandRecorder()
    {
        release_cached_descriptor_tables();
    }

    void RhiDx12CommandRecorder::release_cached_descriptor_tables()
    {
        if (!device_ || !descriptor_tables_) {
            return;
        }
        for (const DescriptorTableCache::Entry& entry
             : descriptor_tables_->entries)
        {
            wz::gpu::dx12::internal::release_compute_buffer_srv_table(
                *device_,
                entry.table);
        }
        descriptor_tables_->entries.clear();
    }

    std::size_t RhiDx12CommandRecorder::cached_descriptor_table_count() const
    {
        return descriptor_tables_ ? descriptor_tables_->entries.size() : 0u;
    }

    void RhiDx12CommandRecorder::barrier(
        wz::rhi::GpuResourceHandle,
        wz::rhi::ResourceState,
        wz::rhi::ResourceState)
    {
        // Pull-cube Stage 2 keeps static buffers in graphics-SRV state after
        // upload. Generic resource-state translation lands with the scene path.
    }

    void RhiDx12CommandRecorder::set_pipeline(wz::rhi::Tag program)
    {
        ready_ = false;
        current_ = pipelines_ ? pipelines_->realize(program) : nullptr;
        if (!device_ || !current_ || !current_->valid()) {
            return;
        }

        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(*device_);
        if (!cmd) {
            return;
        }

        cmd->SetGraphicsRootSignature(current_->root_signature);
        cmd->SetPipelineState(current_->pipeline_state);

        ID3D12DescriptorHeap* heap =
            wz::gpu::dx12::internal::get_srv_cbv_uav_heap(*device_);
        if (heap) {
            cmd->SetDescriptorHeaps(1, &heap);
        }
        cmd->IASetPrimitiveTopology(
            static_cast<D3D12_PRIMITIVE_TOPOLOGY>(
                current_->primitive_topology));
        ready_ = true;
    }

    void RhiDx12CommandRecorder::set_root_constants(
        std::span<const uint8_t> bytes)
    {
        if (!ready_ || !device_ || !current_
            || !current_->layout.root_constants.valid
            || bytes.empty()
            || bytes.size() % sizeof(uint32_t) != 0)
        {
            return;
        }

        const uint32_t dwords =
            static_cast<uint32_t>(bytes.size() / sizeof(uint32_t));
        if (dwords != current_->layout.root_constants.dword_count) {
            ready_ = false;
            return;
        }

        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(*device_);
        if (!cmd) {
            ready_ = false;
            return;
        }

        cmd->SetGraphicsRoot32BitConstants(
            current_->layout.root_constants.root_parameter_index,
            dwords,
            bytes.data(),
            0);
    }

    void RhiDx12CommandRecorder::bind_resource_group(
        uint32_t slot,
        const wz::rhi::ShaderResourceGroup& group)
    {
        if (!ready_ || !device_ || !current_) {
            return;
        }

        const std::optional<uint32_t> root_param =
            current_->layout.root_param_for_slot(slot);
        if (!root_param || group.binding_slot() != slot) {
            ready_ = false;
            return;
        }

        std::vector<wz::gpu::GPUHandle> buffers;
        buffers.reserve(group.resource_count());
        for (wz::rhi::GpuResourceHandle handle : group.resources()) {
            const wz::gpu::GPUHandle gpu = gpu_handle_for(handle);
            if (!gpu.valid()) {
                ready_ = false;
                return;
            }
            buffers.push_back(gpu);
        }

        const wz::gpu::dx12::DX12DescriptorTable* table =
            descriptor_table_for(slot, std::move(buffers));
        if (!table || !table->valid()) {
            ready_ = false;
            return;
        }

        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(*device_);
        if (!cmd) {
            ready_ = false;
            return;
        }
        cmd->SetGraphicsRootDescriptorTable(*root_param, table->gpu_start);
    }

    void RhiDx12CommandRecorder::set_geometry(
        const wz::rhi::GeometryView&,
        const wz::rhi::StreamBufferIndices&)
    {
        // Vertex-pull path intentionally binds no IA buffers.
    }

    void RhiDx12CommandRecorder::draw(const wz::rhi::DrawArgs& args)
    {
        if (!ready_ || !device_) {
            return;
        }
        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(*device_);
        if (!cmd) {
            ready_ = false;
            return;
        }

        const uint32_t vertex_count =
            args.indexed ? args.index_count : args.vertex_count;
        if (vertex_count == 0u) {
            ready_ = false;
            return;
        }
        cmd->DrawInstanced(
            vertex_count,
            args.instance_count,
            args.first_index,
            0);
    }

    wz::gpu::GPUHandle RhiDx12CommandRecorder::gpu_handle_for(
        wz::rhi::GpuResourceHandle handle) const
    {
        if (!resources_ || !backend_) {
            return {};
        }
        const wz::rhi::GpuResource* resource = resources_->get(handle);
        return resource ? backend_->gpu_handle_for(resource->backend)
                        : wz::gpu::GPUHandle{};
    }

    const wz::gpu::dx12::DX12DescriptorTable*
    RhiDx12CommandRecorder::descriptor_table_for(
        uint32_t slot,
        std::vector<wz::gpu::GPUHandle> buffers)
    {
        if (!descriptor_tables_) {
            return nullptr;
        }

        const auto existing = std::ranges::find_if(
            descriptor_tables_->entries,
            [slot, &buffers](const DescriptorTableCache::Entry& entry) {
                return entry.binding_slot == slot && entry.buffers == buffers;
            });
        if (existing != descriptor_tables_->entries.end()) {
            return &existing->table;
        }

        wz::gpu::dx12::DX12DescriptorTable table{};
        if (!device_
            || !wz::gpu::dx12::internal::create_compute_buffer_srv_table(
                *device_,
                buffers,
                table))
        {
            return nullptr;
        }

        descriptor_tables_->entries.push_back(DescriptorTableCache::Entry{
            slot,
            std::move(buffers),
            table });
        return &descriptor_tables_->entries.back().table;
    }
}
