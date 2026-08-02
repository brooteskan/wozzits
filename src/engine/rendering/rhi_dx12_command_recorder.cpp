#include <engine/rendering/rhi_dx12_command_recorder.h>

#include <gpu/dx12/dx12_descriptor_allocator.h>
#include <gpu/dx12/dx12_internal.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace wz::engine::rendering
{
    namespace
    {
        D3D12_RESOURCE_STATES resource_state(wz::rhi::ResourceState state)
        {
            switch (state) {
            case wz::rhi::ResourceState::Undefined:
                return D3D12_RESOURCE_STATE_COMMON;
            case wz::rhi::ResourceState::RenderTarget:
                return D3D12_RESOURCE_STATE_RENDER_TARGET;
            case wz::rhi::ResourceState::DepthWrite:
                return D3D12_RESOURCE_STATE_DEPTH_WRITE;
            case wz::rhi::ResourceState::ShaderRead:
                return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                    | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            case wz::rhi::ResourceState::UnorderedAccess:
                return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            case wz::rhi::ResourceState::CopySrc:
                return D3D12_RESOURCE_STATE_COPY_SOURCE;
            case wz::rhi::ResourceState::CopyDst:
                return D3D12_RESOURCE_STATE_COPY_DEST;
            case wz::rhi::ResourceState::Present:
                return D3D12_RESOURCE_STATE_PRESENT;
            }
            return D3D12_RESOURCE_STATE_COMMON;
        }
    }

    namespace
    {
        // Map an rhi descriptor kind onto the gpu-local view kind the dx12
        // descriptor-table builder understands. Returns nullopt for kinds the
        // pixel-path recorder cannot bind yet (Sampler — deferred to a follow-up
        // sampler-heap path), so bind_resource_group can fail cleanly rather than
        // silently mis-bind. This is the seam that preserves TextureSRV vs
        // StructuredBufferSRV instead of collapsing to a UAV-or-not bool.
        std::optional<wz::gpu::dx12::internal::DescriptorViewKind>
        to_descriptor_view_kind(wz::rhi::DescriptorKind kind)
        {
            using View = wz::gpu::dx12::internal::DescriptorViewKind;
            switch (kind) {
            case wz::rhi::DescriptorKind::StructuredBufferSRV:
                return View::StructuredBufferSRV;
            case wz::rhi::DescriptorKind::UAV:
                return View::StructuredBufferUAV;
            case wz::rhi::DescriptorKind::TextureSRV:
                return View::Texture2DSRV;
            case wz::rhi::DescriptorKind::Sampler:
                return std::nullopt;
            }
            return std::nullopt;
        }
    }

    struct RhiDx12CommandRecorder::DescriptorTableCache
    {
        struct Entry
        {
            uint32_t binding_slot = 0;
            std::vector<wz::gpu::GPUHandle> resources;
            std::vector<wz::gpu::dx12::internal::DescriptorViewKind> kinds;
            wz::gpu::dx12::DX12DescriptorTable table{};
        };

        std::vector<Entry> entries;
    };

    RhiDx12CommandRecorder::RhiDx12CommandRecorder(
        wz::gpu::Device& device,
        RhiDx12PipelineCache& pipelines,
        wz::rhi::GpuResourceRegistry& resources,
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
        wz::rhi::GpuResourceHandle resource,
        wz::rhi::ResourceState from,
        wz::rhi::ResourceState to)
    {
        if (!device_ || !resource.valid()) {
            return;
        }

        // This frame's command list will reference the resource; record the
        // frame's timeline so the registry keeps it resident until the GPU has
        // passed this frame (a no-op for a stale handle).
        if (resources_) {
            resources_->touch(resource, frame_timeline_);
        }

        const wz::gpu::GPUHandle gpu = gpu_handle_for(resource);
        if (!gpu.valid()) {
            return;
        }

        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(*device_);
        if (!cmd) {
            ready_ = false;
            last_reject_reason_ = "barrier: no command list";
            return;
        }

        if (from == wz::rhi::ResourceState::UnorderedAccess
            && to == wz::rhi::ResourceState::UnorderedAccess)
        {
            if (!wz::gpu::dx12::internal::uav_barrier_compute_buffer(
                    *device_,
                    cmd,
                    gpu))
            {
                ready_ = false;
                last_reject_reason_ = "barrier: UAV barrier failed";
            }
            return;
        }

        if (!wz::gpu::dx12::internal::transition_compute_buffer(
                *device_,
                cmd,
                gpu,
                resource_state(to)))
        {
            ready_ = false;
            last_reject_reason_ = "barrier: resource transition failed";
        }
    }

    void RhiDx12CommandRecorder::set_pipeline(wz::rhi::Tag program)
    {
        ready_ = false;
        last_reject_reason_.clear();
        last_reject_stale_resource_ = false;
        current_ = pipelines_ ? pipelines_->realize(program) : nullptr;
        if (!device_ || !current_ || !current_->valid()) {
            last_reject_reason_ =
                !device_    ? "set_pipeline: no device"
                : !current_ ? "set_pipeline: realize returned null (see the "
                              "RhiDx12PipelineCache::realize error above)"
                            : "set_pipeline: pipeline realized but invalid()";
            return;
        }

        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(*device_);
        if (!cmd) {
            last_reject_reason_ = "set_pipeline: no command list";
            return;
        }

        if (current_->is_compute) {
            cmd->SetComputeRootSignature(current_->root_signature);
        }
        else {
            cmd->SetGraphicsRootSignature(current_->root_signature);
        }
        cmd->SetPipelineState(current_->pipeline_state);

        ID3D12DescriptorHeap* heap =
            wz::gpu::dx12::internal::get_srv_cbv_uav_heap(*device_);
        if (heap) {
            cmd->SetDescriptorHeaps(1, &heap);
        }
        if (!current_->is_compute) {
            cmd->IASetPrimitiveTopology(
                static_cast<D3D12_PRIMITIVE_TOPOLOGY>(
                    current_->primitive_topology));
        }
        ready_ = true;
    }

    void RhiDx12CommandRecorder::set_root_constants(
        std::span<const uint8_t> bytes)
    {
        // Structural preconditions: nothing to do, and nothing to report.
        // A program whose root signature has no root-constant parameter is the
        // normal case for a packet that carries a block anyway.
        if (!ready_ || !device_ || !current_
            || !current_->layout.root_constants.valid)
        {
            return;
        }

        // A block whose size is not a whole number of DWORDs is malformed, and
        // it used to be dropped IN SILENCE -- returning without touching
        // ready_, so record_packet went straight on to draw() and the draw
        // issued with whatever root constants the PREVIOUS packet pushed. The
        // dword-count mismatch nine lines below has always rejected loudly.
        // Two spellings of the same failure -- the object is drawn with someone
        // else's transform -- one visible and one not, decided by which half of
        // one `if` the predicate happened to sit in (#317).
        if (bytes.size() % sizeof(uint32_t) != 0) {
            ready_ = false;
            last_reject_reason_ =
                "set_root_constants: block is not a whole number of dwords ("
                + std::to_string(bytes.size()) + " bytes)";
            return;
        }

        const uint32_t dwords =
            static_cast<uint32_t>(bytes.size() / sizeof(uint32_t));
        if (dwords != current_->layout.root_constants.dword_count) {
            ready_ = false;
            last_reject_reason_ =
                "set_root_constants: dword count mismatch (packet has "
                + std::to_string(dwords) + ", root signature expects "
                + std::to_string(current_->layout.root_constants.dword_count)
                + ")";
            return;
        }

        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(*device_);
        if (!cmd) {
            ready_ = false;
            last_reject_reason_ = "set_root_constants: no command list";
            return;
        }

        if (current_->is_compute) {
            cmd->SetComputeRoot32BitConstants(
                current_->layout.root_constants.root_parameter_index,
                dwords,
                bytes.data(),
                0);
        }
        else {
            cmd->SetGraphicsRoot32BitConstants(
                current_->layout.root_constants.root_parameter_index,
                dwords,
                bytes.data(),
                0);
        }
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
            last_reject_reason_ =
                !root_param
                    ? "bind_resource_group: slot " + std::to_string(slot)
                        + " has no root parameter in the pipeline layout"
                    : "bind_resource_group: group binding_slot "
                        + std::to_string(group.binding_slot())
                        + " does not match requested slot "
                        + std::to_string(slot);
            return;
        }

        const auto table_layout = std::ranges::find_if(
            current_->layout.descriptor_tables,
            [slot](const RhiDx12DescriptorTableParam& candidate) {
                return candidate.binding_slot == slot;
            });
        if (table_layout == current_->layout.descriptor_tables.end()
            || table_layout->descriptor_kinds.size() != group.resource_count())
        {
            ready_ = false;
            last_reject_reason_ =
                table_layout == current_->layout.descriptor_tables.end()
                    ? "bind_resource_group: slot " + std::to_string(slot)
                        + " has no descriptor table in the pipeline layout"
                    : "bind_resource_group: slot " + std::to_string(slot)
                        + " resource count mismatch (group has "
                        + std::to_string(group.resource_count())
                        + ", layout expects "
                        + std::to_string(table_layout->descriptor_kinds.size())
                        + ")";
            return;
        }

        std::vector<wz::gpu::GPUHandle> resources;
        resources.reserve(group.resource_count());
        for (wz::rhi::GpuResourceHandle handle : group.resources()) {
            // Touch every handle in the group BEFORE the descriptor-table cache
            // lookup below: this frame's command list will reference them, so
            // the registry must keep them resident until the GPU has passed the
            // frame. It runs on the resolution loop so a cache HIT (a long-lived
            // cached table) still refreshes last_use — otherwise the table's
            // resources would be collected while it is still bound.
            if (resources_) {
                resources_->touch(handle, frame_timeline_);
            }
            const wz::gpu::GPUHandle gpu = gpu_handle_for(handle);
            if (!gpu.valid()) {
                ready_ = false;
                last_reject_stale_resource_ = true;
                last_reject_reason_ =
                    "bind_resource_group: slot " + std::to_string(slot)
                    + " resource #" + std::to_string(resources.size())
                    + " has no live GPU handle (buffer not resident / not "
                      "registered at render time)";
                return;
            }
            resources.push_back(gpu);
        }

        std::vector<wz::gpu::dx12::internal::DescriptorViewKind> kinds;
        kinds.reserve(table_layout->descriptor_kinds.size());
        for (const wz::rhi::DescriptorKind kind
             : table_layout->descriptor_kinds)
        {
            const std::optional view = to_descriptor_view_kind(kind);
            if (!view) {
                // Unsupported descriptor kind (Sampler): no heap/root-param path
                // yet. Fail closed rather than bind the wrong view.
                ready_ = false;
                last_reject_reason_ =
                    "bind_resource_group: slot " + std::to_string(slot)
                    + " has an unsupported descriptor kind (Sampler has no "
                      "pixel-path binding yet)";
                return;
            }
            kinds.push_back(*view);
        }

        const wz::gpu::dx12::DX12DescriptorTable* table =
            descriptor_table_for(
                slot,
                std::move(resources),
                std::move(kinds));
        if (!table || !table->valid()) {
            ready_ = false;
            last_reject_reason_ =
                "bind_resource_group: slot " + std::to_string(slot)
                + " failed to build a descriptor table";
            return;
        }

        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(*device_);
        if (!cmd) {
            ready_ = false;
            last_reject_reason_ =
                "bind_resource_group: slot " + std::to_string(slot)
                + " no command list";
            return;
        }
        if (current_->is_compute) {
            cmd->SetComputeRootDescriptorTable(*root_param, table->gpu_start);
        }
        else {
            cmd->SetGraphicsRootDescriptorTable(*root_param, table->gpu_start);
        }
    }

    void RhiDx12CommandRecorder::set_geometry(
        const wz::rhi::GeometryView&,
        const wz::rhi::StreamBufferIndices&)
    {
        // Vertex-pull path intentionally binds no IA buffers.
    }

    void RhiDx12CommandRecorder::draw(const wz::rhi::DrawArgs& args)
    {
        if (!ready_ || !device_ || !current_ || current_->is_compute) {
            return;
        }
        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(*device_);
        if (!cmd) {
            ready_ = false;
            last_reject_reason_ = "draw: no command list";
            return;
        }

        const uint32_t vertex_count =
            args.indexed ? args.index_count : args.vertex_count;
        if (vertex_count == 0u) {
            ready_ = false;
            last_reject_reason_ =
                args.indexed ? "draw: index_count is 0"
                             : "draw: vertex_count is 0";
            return;
        }
        cmd->DrawInstanced(
            vertex_count,
            args.instance_count,
            args.first_index,
            0);
    }

    void RhiDx12CommandRecorder::dispatch(
        const wz::rhi::DispatchArgs& args)
    {
        if (!ready_ || !device_ || !current_ || !current_->is_compute) {
            return;
        }
        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(*device_);
        if (!cmd) {
            ready_ = false;
            return;
        }

        cmd->Dispatch(
            args.group_count[0],
            args.group_count[1],
            args.group_count[2]);
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
        std::vector<wz::gpu::GPUHandle> resources,
        std::vector<wz::gpu::dx12::internal::DescriptorViewKind> kinds)
    {
        if (!descriptor_tables_) {
            return nullptr;
        }

        const auto existing = std::ranges::find_if(
            descriptor_tables_->entries,
            [slot, &resources, &kinds](
                const DescriptorTableCache::Entry& entry) {
                return entry.binding_slot == slot
                    && entry.resources == resources
                    && entry.kinds == kinds;
            });
        if (existing != descriptor_tables_->entries.end()) {
            return &existing->table;
        }

        wz::gpu::dx12::DX12DescriptorTable table{};
        if (!device_
            || !wz::gpu::dx12::internal::create_resource_descriptor_table(
                *device_,
                resources,
                kinds,
                table))
        {
            return nullptr;
        }

        descriptor_tables_->entries.push_back(DescriptorTableCache::Entry{
            slot,
            std::move(resources),
            std::move(kinds),
            table });
        return &descriptor_tables_->entries.back().table;
    }
}
