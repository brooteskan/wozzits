#pragma once

// engine/rendering/rhi_dx12_command_recorder.h
//
// DX12 implementation of the rhi CommandRecorder for the pixel-path slice.
// It consumes already-realized rhi pipelines and value-type SRGs.

#include <engine/rendering/rhi_dx12_pipeline.h>
#include <engine/rendering/rhi_gpu_backend.h>

#include <wozzits/rhi/frame_graph.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace wz::gpu::dx12
{
    struct DX12DescriptorTable;
}

namespace wz::engine::rendering
{
    class RhiDx12CommandRecorder final : public wz::rhi::CommandRecorder
    {
    public:
        RhiDx12CommandRecorder(
            wz::gpu::Device& device,
            RhiDx12PipelineCache& pipelines,
            const wz::rhi::GpuResourceRegistry& resources,
            const EngineGpuBackend& backend);
        ~RhiDx12CommandRecorder() override;

        RhiDx12CommandRecorder(const RhiDx12CommandRecorder&) = delete;
        RhiDx12CommandRecorder& operator=(const RhiDx12CommandRecorder&) = delete;

        void barrier(wz::rhi::GpuResourceHandle resource,
                     wz::rhi::ResourceState from,
                     wz::rhi::ResourceState to) override;
        void set_pipeline(wz::rhi::Tag program) override;
        void set_root_constants(std::span<const uint8_t> bytes) override;
        void bind_resource_group(
            uint32_t slot,
            const wz::rhi::ShaderResourceGroup& group) override;
        void set_geometry(const wz::rhi::GeometryView& geometry,
                          const wz::rhi::StreamBufferIndices& streams) override;
        void draw(const wz::rhi::DrawArgs& args) override;

        [[nodiscard]] bool ready() const noexcept { return ready_; }

#ifdef WZ_ENABLE_TESTING
        void set_current_for_testing(
            const RhiDx12RealizedPipeline* pipeline) noexcept
        {
            current_ = pipeline;
            ready_ = pipeline != nullptr;
        }
#endif

    private:
        struct DescriptorTableCache;

        [[nodiscard]] wz::gpu::GPUHandle gpu_handle_for(
            wz::rhi::GpuResourceHandle handle) const;
        [[nodiscard]] const wz::gpu::dx12::DX12DescriptorTable*
        descriptor_table_for(uint32_t slot,
                             std::vector<wz::gpu::GPUHandle> buffers);

        wz::gpu::Device* device_ = nullptr;
        RhiDx12PipelineCache* pipelines_ = nullptr;
        const wz::rhi::GpuResourceRegistry* resources_ = nullptr;
        const EngineGpuBackend* backend_ = nullptr;
        const RhiDx12RealizedPipeline* current_ = nullptr;
        bool ready_ = true;
        std::unique_ptr<DescriptorTableCache> descriptor_tables_;
    };
}
