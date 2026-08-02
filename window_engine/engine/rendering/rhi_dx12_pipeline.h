#pragma once

// engine/rendering/rhi_dx12_pipeline.h
//
// Stage-1 rhi-native DX12 pipeline realization. The pure planning function is
// unit-testable without a device; the cache realizes PSO/root-signature pairs
// from rhi RenderProgramDesc + ShaderModuleRegistry when a real device exists.

#include <gpu/gpu.h>

#include <wozzits/rhi/compute_program.h>
#include <wozzits/rhi/render_program_registry.h>
#include <wozzits/rhi/shader_module.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

struct ID3D12PipelineState;
struct ID3D12RootSignature;

namespace wz { struct Logger; }

namespace wz::engine::rendering
{
    // The HLSL input-element semantic a rhi VertexAttribute::location maps to.
    //
    // D3D12 requires every (name, index) pair in an input layout to be UNIQUE,
    // and the mapping used to return a bare name with SemanticIndex hardcoded
    // 0 -- so locations 2 and 4 both became ("TEXCOORD", 0) and
    // CreateInputLayout rejected the whole PSO with "All Semantics in the Input
    // Layout must be unique". That is exactly what InputLayoutKind::
    // GaussianSplatVertex declares (locations 0..4), so the shipped
    // BuiltinRenderProgram::GaussianSplatDebug preset could not create a
    // pipeline at all (#317).
    //
    // The mapping is now INJECTIVE by construction: 0..3 keep the conventional
    // names at index 0, and every location past 3 is TEXCOORD(location - 3),
    // which cannot collide with location 2's TEXCOORD0. Exposed (rather than
    // living in the .cpp's anonymous namespace) so the uniqueness property is
    // testable without a device -- being untestable is why the collision
    // survived.
    struct Dx12InputElementSemantic
    {
        const char* name  = "TEXCOORD";
        uint32_t    index = 0;
    };

    [[nodiscard]] constexpr Dx12InputElementSemantic
    dx12_input_element_semantic(uint32_t location) noexcept
    {
        switch (location) {
        case 0: return { "POSITION", 0 };
        case 1: return { "NORMAL",   0 };
        case 2: return { "TEXCOORD", 0 };
        case 3: return { "COLOR",    0 };
        default: break;
        }
        return { "TEXCOORD", location - 3u };
    }

    struct RhiDx12DescriptorTableParam
    {
        uint32_t binding_slot = 0;
        uint32_t root_parameter_index = 0;
        uint32_t descriptor_count = 0;
        std::vector<wz::rhi::DescriptorKind> descriptor_kinds;
    };

    struct RhiDx12RootConstantsParam
    {
        bool valid = false;
        uint32_t root_parameter_index = 0;
        uint32_t dword_count = 0;
        uint32_t shader_register = 0;
        uint32_t register_space = 0;
        wz::rhi::ShaderStage visibility = wz::rhi::ShaderStage::All;
    };

    struct RhiDx12PipelineLayout
    {
        std::vector<RhiDx12DescriptorTableParam> descriptor_tables;
        RhiDx12RootConstantsParam root_constants;

        [[nodiscard]] std::optional<uint32_t> root_param_for_slot(
            uint32_t binding_slot) const noexcept;
    };

    // A D3D12 root signature may total at most 64 DWORDs. This is a hardware
    // constant, not a tunable -- it is here rather than in wozzits-rhi because
    // it is a D3D12 property and rhi is device-free.
    inline constexpr uint32_t kDx12MaxRootSignatureDwords = 64;

    // What a planned layout costs against that budget: one DWORD per 32-bit
    // root constant, one per descriptor table (a table is a single root
    // parameter regardless of how many descriptors it points at). Pure, so a
    // test can walk the boundary without a device -- which matters because
    // D3D12SerializeRootSignature returns S_OK for an over-budget signature
    // and only CreateRootSignature rejects it, with a bare E_INVALIDARG and no
    // error blob. See #317.
    [[nodiscard]] constexpr uint32_t dx12_root_signature_dword_cost(
        const RhiDx12PipelineLayout& layout) noexcept
    {
        uint32_t dwords =
            static_cast<uint32_t>(layout.descriptor_tables.size());
        if (layout.root_constants.valid) {
            dwords += layout.root_constants.dword_count;
        }
        return dwords;
    }

    struct RhiDx12RealizedPipeline
    {
        ID3D12RootSignature* root_signature = nullptr;
        ID3D12PipelineState* pipeline_state = nullptr;
        RhiDx12PipelineLayout layout;
        uint32_t primitive_topology = 0;
        bool is_compute = false;

        [[nodiscard]] bool valid() const noexcept
        {
            return root_signature && pipeline_state;
        }
    };

    [[nodiscard]] std::optional<RhiDx12PipelineLayout>
    plan_dx12_pipeline_layout(
        std::span<const wz::rhi::ShaderResourceGroupLayout>
            shader_resource_groups);

    [[nodiscard]] inline std::optional<RhiDx12PipelineLayout>
    plan_dx12_pipeline_layout(const wz::rhi::RenderProgramDesc& program)
    {
        return plan_dx12_pipeline_layout(
            std::span<const wz::rhi::ShaderResourceGroupLayout>{
                program.shader_resource_groups.data(),
                program.shader_resource_groups.size() });
    }

    [[nodiscard]] inline std::optional<RhiDx12PipelineLayout>
    plan_dx12_pipeline_layout(const wz::rhi::ComputeProgramDesc& program)
    {
        return plan_dx12_pipeline_layout(
            std::span<const wz::rhi::ShaderResourceGroupLayout>{
                program.shader_resource_groups.data(),
                program.shader_resource_groups.size() });
    }

    class RhiDx12PipelineCache
    {
    public:
        RhiDx12PipelineCache(wz::gpu::Device& device,
                             const wz::rhi::RenderProgramRegistry& programs,
                             const wz::rhi::ComputeProgramRegistry& compute_programs,
                             const wz::rhi::ShaderModuleRegistry& shaders,
                             wz::Logger* logger = nullptr);
        ~RhiDx12PipelineCache();

        RhiDx12PipelineCache(const RhiDx12PipelineCache&) = delete;
        RhiDx12PipelineCache& operator=(const RhiDx12PipelineCache&) = delete;

        // Realize (or find) the pipeline for `program` AS THE CURRENTLY BOUND
        // PASS needs it. Whether that pass has a depth-stencil view is part of
        // the key: D3D12 refuses a non-UNKNOWN DSVFormat against a null DSV, so
        // a program drawn both into the backbuffer and into an authored render
        // target needs two pipelines. The flag is read from the device, not
        // passed in, so it cannot disagree with what OMSetRenderTargets did.
        [[nodiscard]] const RhiDx12RealizedPipeline* realize(
            wz::rhi::Tag program);
        [[nodiscard]] const RhiDx12RealizedPipeline* get(
            wz::rhi::Tag program) const noexcept;
        [[nodiscard]] const RhiDx12RealizedPipeline* get(
            wz::rhi::Tag program, bool depth_target_bound) const noexcept;

        void clear() noexcept;

    private:
        struct Entry
        {
            wz::rhi::Tag program{};
            // See realize(). Half of the cache key, not a property of the
            // program -- the same program yields a different pipeline
            // depending on what the pass bound.
            bool depth_target_bound = false;
            RhiDx12RealizedPipeline realized{};
        };

        wz::gpu::Device* device_ = nullptr;
        const wz::rhi::RenderProgramRegistry* programs_ = nullptr;
        const wz::rhi::ComputeProgramRegistry* compute_programs_ = nullptr;
        const wz::rhi::ShaderModuleRegistry* shaders_ = nullptr;
        wz::Logger* logger_ = nullptr;
        std::vector<Entry> entries_;
    };
}
