#pragma once
// engine/rendering/render_program_pipeline_cache.h
//
// Runtime cache: maps RenderProgramTable handles to realized DX12 pipelines.
//
// Separation of concerns:
//   RenderProgramData  — CPU-side declarative asset (what the program needs).
//   RenderProgramPipelineCache — GPU-side realized state (root sig + PSO).
//
// The cache does not own the GPU pipeline objects; those are owned by the
// device's DX12GraphicsPipelineTable.  clear() only forgets the mappings.

#include <asset/types.h>
#include <gpu/gpu.h>
#include <engine/assets/render_program/render_program.h>

#include <optional>
#include <vector>

namespace wz::engine::rendering
{
    class RenderProgramPipelineCache
    {
    public:
        // Realize a pipeline for the given render program handle.
        // Looks up RenderProgramData from the table, creates a DX12 root
        // signature and PSO, and caches the resulting GPUHandle.
        // Returns true if the pipeline is now available (including if it was
        // already realized).  Returns false on lookup or creation failure.
        bool realize(
            wz::gpu::Device&                              device,
            const wz::engine::assets::RenderProgramTable& table,
            wz::asset::ResourceHandle                     render_program);

        // Returns the GPU pipeline handle, or an invalid handle if not yet
        // realized.
        wz::gpu::GPUHandle get(
            wz::asset::ResourceHandle render_program) const noexcept;

        // Returns the binding model recorded at realize() time, or nullopt
        // if the handle has not been realized.  A valid handle that is absent
        // from the cache means the pipeline was never realized — callers
        // should skip/log rather than falling back to a default.
        std::optional<wz::engine::assets::RenderBindingModel> get_binding_model(
            wz::asset::ResourceHandle render_program) const noexcept;

        // Forget all cached mappings.  Does not release GPU resources.
        void clear() noexcept;

    private:
        struct Entry
        {
            wz::asset::ResourceHandle                    render_program{};
            wz::gpu::GPUHandle                           pipeline{};
            wz::engine::assets::RenderBindingModel       binding_model{};
        };

        std::vector<Entry> entries_;
    };
}
