#pragma once

// engine/rendering/rhi_dx12_command_recorder.h
//
// DX12 implementation of the rhi CommandRecorder for the pixel-path slice.
// It consumes already-realized rhi pipelines and value-type SRGs.

#include <engine/rendering/rhi_dx12_pipeline.h>
#include <engine/rendering/rhi_gpu_backend.h>

#include <wozzits/rhi/frame_graph.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wz::gpu::dx12
{
    struct DX12DescriptorTable;
}

namespace wz::gpu::dx12::internal
{
    enum class DescriptorViewKind : uint8_t;
}

namespace wz::engine::rendering
{
    class RhiDx12CommandRecorder final : public wz::rhi::CommandRecorder
    {
    public:
        RhiDx12CommandRecorder(
            wz::gpu::Device& device,
            RhiDx12PipelineCache& pipelines,
            wz::rhi::GpuResourceRegistry& resources,
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
        void dispatch(const wz::rhi::DispatchArgs& args) override;

        [[nodiscard]] bool ready() const noexcept { return ready_; }

        // Diagnostic: why the most-recently-recorded packet was rejected (empty
        // when the last packet recorded cleanly). Set at every point that drops
        // ready_ to false and cleared at the start of each set_pipeline (i.e.
        // once per packet), so it names the exact failing record step — root
        // constants, an SRG bind, or the draw — instead of a generic reject.
        [[nodiscard]] const std::string& last_reject_reason() const noexcept
        {
            return last_reject_reason_;
        }

        // True when the last rejection was a STALE RESOURCE: an SRG resource
        // whose registry entry is gone (released + collected, or device loss).
        // Distinguished from structural rejects (layout mismatch etc.) because
        // the caller's correct response differs: a stale resource is healed by
        // re-realizing the renderable (re-acquire rebuilds the buffer), while a
        // structural reject would just fail again. Cleared per packet in
        // set_pipeline, like last_reject_reason.
        [[nodiscard]] bool last_reject_was_stale_resource() const noexcept
        {
            return last_reject_stale_resource_;
        }

        // Release every cached SRV descriptor table and drop the cache. The
        // cache keys tables by the engine GPUHandles of the buffers they view;
        // a graph swap retires those buffers, so the tables must be released
        // too — otherwise descriptor-heap ranges leak across swaps and a table
        // could be reused for a recycled handle. Caller must have flushed the
        // GPU (wait_idle) first, since in-flight command lists may reference them.
        void release_cached_descriptor_tables();

        // Number of SRV descriptor tables currently cached. Drops to 0 after
        // release_cached_descriptor_tables(); the rebind test asserts on this.
        [[nodiscard]] std::size_t cached_descriptor_table_count() const;

        // Set the GPU timeline value the frame being recorded will be signaled
        // with — every rhi resource this recorder resolves is touched with it
        // so the registry keeps the resource resident until the GPU has passed
        // the frame. The renderer sets this once per frame, before recording.
        void set_frame_timeline(uint64_t value) noexcept
        {
            frame_timeline_ = value;
        }

#ifdef WZ_ENABLE_TESTING
        void set_current_for_testing(
            const RhiDx12RealizedPipeline* pipeline) noexcept
        {
            current_ = pipeline;
            ready_ = pipeline != nullptr;
        }

        // The arguments of one ID3D12GraphicsCommandList::DrawInstanced call,
        // recorded at the call site itself. Ground truth for "how many draws
        // does a frame issue, and with what instance count" -- questions the
        // #288 ladder register could previously only answer by reading code.
        struct CapturedDraw
        {
            // As handed to DrawInstanced, in its argument order.
            uint32_t vertex_count_per_instance = 0;
            uint32_t instance_count = 0;
            uint32_t start_vertex_location = 0;
            // The DrawArgs that produced them, so a capture can tell an
            // indexed draw of index_count from a non-indexed one of the same
            // width (the DX12 path draws both with DrawInstanced -- the pull
            // vertex shader indexes for itself).
            bool     indexed = false;
            uint32_t index_count = 0;
            uint32_t source_vertex_count = 0;
        };

        // Point the recorder at a sink to append to, or nullptr to stop. The
        // sink is never cleared here -- the caller owns when a capture starts,
        // so one vector can span several frames or one.
        void set_draw_capture(std::vector<CapturedDraw>* sink) noexcept
        {
            draw_capture_ = sink;
        }
#endif

    private:
        struct DescriptorTableCache;

        [[nodiscard]] wz::gpu::GPUHandle gpu_handle_for(
            wz::rhi::GpuResourceHandle handle) const;

        // Release and forget every cached table that views a resource the rhi
        // registry no longer has. Called on a cache MISS -- the only moment
        // the cache grows, and so the only one worth paying a sweep for; a HIT
        // cannot be stale, because a released handle stops resolving and its
        // entry is precisely what this drops. See the Entry comment in the .cpp
        // for why keying on rhi handles is what makes this question askable at
        // all (#317).
        void drop_dead_descriptor_tables();

        // `resources` (rhi handles) keys the cache; `gpu_resources` (the
        // resolved engine handles, index-aligned) builds the table.
        [[nodiscard]] const wz::gpu::dx12::DX12DescriptorTable*
        descriptor_table_for(
            uint32_t slot,
            std::vector<wz::rhi::GpuResourceHandle> resources,
            const std::vector<wz::gpu::GPUHandle>& gpu_resources,
            std::vector<wz::gpu::dx12::internal::DescriptorViewKind> kinds);

        wz::gpu::Device* device_ = nullptr;
        RhiDx12PipelineCache* pipelines_ = nullptr;
        wz::rhi::GpuResourceRegistry* resources_ = nullptr;
        const EngineGpuBackend* backend_ = nullptr;
        const RhiDx12RealizedPipeline* current_ = nullptr;
        bool ready_ = true;
        // Diagnostic reason for the last ready_ = false (see last_reject_reason).
        std::string last_reject_reason_;
        // See last_reject_was_stale_resource().
        bool last_reject_stale_resource_ = false;
        // The value end_frame will signal for the frame being recorded; the
        // renderer sets it once per frame via set_frame_timeline. Resources
        // resolved by this recorder are touched with it (see bind_resource_group
        // and barrier) so precise reclamation keeps them until the GPU passes it.
        uint64_t frame_timeline_ = 0;
        std::unique_ptr<DescriptorTableCache> descriptor_tables_;
#ifdef WZ_ENABLE_TESTING
        // See set_draw_capture. Null in every build that has not asked.
        std::vector<CapturedDraw>* draw_capture_ = nullptr;
#endif
    };
}
