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

        [[nodiscard]] const RhiDx12RealizedPipeline* realize(
            wz::rhi::Tag program);
        [[nodiscard]] const RhiDx12RealizedPipeline* get(
            wz::rhi::Tag program) const noexcept;

        void clear() noexcept;

    private:
        struct Entry
        {
            wz::rhi::Tag program{};
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
