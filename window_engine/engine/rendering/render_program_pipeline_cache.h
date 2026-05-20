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

#include <cstdint>
#include <optional>
#include <vector>

namespace wz::engine::rendering
{
    // ── Realized binding layout ───────────────────────────────────────────────
    //
    // Computed at realize() time from RenderProgramData.  Encodes the DX12 root
    // parameter index for each declared binding so the submit path never needs
    // to hardcode root parameter numbers.

    struct RealizedRootConstantBinding
    {
        uint32_t root_parameter_index = 0;
        uint32_t value_count          = 0;
        uint32_t shader_register      = 0;
        uint32_t register_space       = 0;
    };

    // Per-binding descriptor record: used by the submit path to locate a
    // specific semantic (e.g. SplatCloud) within the heap table.
    struct RealizedDescriptorBinding
    {
        wz::engine::assets::DescriptorSemantic semantic{};
        uint32_t root_parameter_index    = 0;   // which root param holds the table
        uint32_t descriptor_table_offset = 0;   // slot offset within srv_table
    };

    // Per-visibility-group record: one SetGraphicsRootDescriptorTable call per
    // entry, using srv_table.gpu_at(heap_start_slot) as the base address.
    struct RealizedDescriptorTableBinding
    {
        uint32_t root_parameter_index = 0;
        uint32_t heap_start_slot      = 0;  // offset into the cloud's srv_table
        uint32_t slot_count           = 0;  // number of contiguous descriptors
    };

    struct RealizedRenderProgramBindingLayout
    {
        std::vector<RealizedRootConstantBinding>    root_constants;
        std::vector<RealizedDescriptorBinding>      descriptors;   // lookup by semantic
        std::vector<RealizedDescriptorTableBinding> desc_tables;   // one per visibility group

        bool valid() const noexcept
        {
            return !root_constants.empty() || !desc_tables.empty();
        }
    };
}

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

        // Returns the realized binding layout recorded at realize() time, or
        // nullptr if the handle has not been realized.  The submit path uses
        // this to look up root parameter indices without hardcoding them.
        const RealizedRenderProgramBindingLayout* get_binding_layout(
            wz::asset::ResourceHandle render_program) const noexcept;

        // Forget all cached mappings.  Does not release GPU resources.
        void clear() noexcept;

    private:
        struct Entry
        {
            wz::asset::ResourceHandle                    render_program{};
            wz::gpu::GPUHandle                           pipeline{};
            wz::engine::assets::RenderBindingModel       binding_model{};
            RealizedRenderProgramBindingLayout           binding_layout{};
        };

        std::vector<Entry> entries_;
    };
}
