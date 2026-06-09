#pragma once

// engine/behavior/behavior_gpu_compute_executor.h

#include <engine/behavior/behavior_gpu_compute.h>
#include <engine/behavior/behavior_registry.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/compute_pipeline/compute_pipeline.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <gpu/compute.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::behavior
{
    enum class BehaviorGpuKernelPortTarget : uint8_t
    {
        BufferBinding,
        RootConstant,
    };

    struct BehaviorGpuKernelPortBinding
    {
        std::string name;
        WzGpuPortKind port_kind = WZ_GPU_PORT_NONE;
        WzGpuPortDirection direction = 0u;
        BehaviorGpuKernelPortTarget target =
            BehaviorGpuKernelPortTarget::BufferBinding;
        wz::engine::assets::ComputeBindingKind binding_kind{};
        uint32_t shader_register = 0u;
        uint32_t register_space = 0u;
        uint32_t stride_bytes = 0u;
        uint32_t root_constant_offset = 0u;
        uint32_t root_constant_dwords = 0u;
    };

    struct BehaviorGpuKernelBinding
    {
        std::string name;
        wz::gpu::GPUHandle pipeline{};
        uint32_t root_constant_dwords = 0u;
        std::vector<BehaviorGpuKernelPortBinding> ports;
    };

    struct BehaviorGpuKernelLibrary
    {
        std::vector<BehaviorGpuKernelBinding> kernels;

        [[nodiscard]] const BehaviorGpuKernelBinding* find(
            const std::string& name) const;
    };

    struct BehaviorGpuOutputReadback
    {
        WzGpuWorkId work{};
        std::string port_name;
        WzGpuPortKind kind = WZ_GPU_PORT_NONE;
        uint32_t element_count = 0u;
        uint32_t stride_bytes = 0u;
        std::vector<std::byte> bytes;
    };

    struct BehaviorGpuDispatchReport
    {
        uint32_t submitted = 0u;
        uint32_t dispatched = 0u;
        uint32_t failed = 0u;
        std::vector<BehaviorGpuOutputReadback> readbacks;
        std::vector<WzGpuWorkId> completed_work;
        std::vector<WzGpuWorkId> failed_work;
    };

    BehaviorGpuDispatchReport dispatch_behavior_gpu_compute_jobs(
        wz::gpu::Device& device,
        std::span<const BehaviorGpuComputeJob> jobs,
        std::span<const BehaviorGpuKernelBinding> kernels);

    BehaviorGpuDispatchReport dispatch_behavior_gpu_compute_jobs(
        wz::gpu::Device& device,
        std::span<const BehaviorGpuComputeJob> jobs,
        const BehaviorGpuKernelLibrary& library);

    bool build_kernel_library_from_scene(
        wz::gpu::Device& device,
        const wz::engine::assets::SceneAssetData& scene,
        const wz::engine::assets::EngineAssetLibrary& assets,
        std::span<const BehaviorGpuKernelContract> contracts,
        BehaviorGpuKernelLibrary& out_library,
        std::string* error = nullptr);

    bool build_kernel_library_from_scene(
        wz::gpu::Device& device,
        const wz::engine::assets::SceneAssetData& scene,
        const wz::engine::assets::EngineAssetLibrary& assets,
        BehaviorGpuKernelLibrary& out_library,
        std::string* error = nullptr);

    uint32_t release_behavior_gpu_kernel_library(
        wz::gpu::Device& device,
        BehaviorGpuKernelLibrary& library);

    uint32_t post_behavior_gpu_compute_events(
        BehaviorGpuComputeBuffer& buffer,
        std::span<const BehaviorGpuComputeJob> jobs,
        const BehaviorGpuDispatchReport& report);
}
