#pragma once

// engine/rendering/rhi_dx12_pipeline.h
//
// Stage-1 rhi-native DX12 pipeline realization. The pure planning function is
// unit-testable without a device; the cache realizes PSO/root-signature pairs
// from rhi RenderProgramDesc + ShaderModuleRegistry when a real device exists.

#include <gpu/gpu.h>

#include <wozzits/rhi/render_program_registry.h>
#include <wozzits/rhi/shader_module.h>

#include <cstdint>
#include <optional>
#include <vector>

struct ID3D12PipelineState;
struct ID3D12RootSignature;

namespace wz::engine::rendering
{
    struct RhiDx12DescriptorTableParam
    {
        uint32_t binding_slot = 0;
        uint32_t root_parameter_index = 0;
        uint32_t descriptor_count = 0;
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

        [[nodiscard]] bool valid() const noexcept
        {
            return root_signature && pipeline_state;
        }
    };

    [[nodiscard]] std::optional<RhiDx12PipelineLayout>
    plan_dx12_pipeline_layout(const wz::rhi::RenderProgramDesc& program);

    class RhiDx12PipelineCache
    {
    public:
        RhiDx12PipelineCache(wz::gpu::Device& device,
                             const wz::rhi::RenderProgramRegistry& programs,
                             const wz::rhi::ShaderModuleRegistry& shaders);
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
        const wz::rhi::ShaderModuleRegistry* shaders_ = nullptr;
        std::vector<Entry> entries_;
    };
}
