#pragma once

// engine/assets/compute_pipeline/compute_pipeline.h

#include <asset/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    enum class ComputeBindingKind : uint8_t
    {
        StructuredBufferSRV,
        StructuredBufferUAV,
        ByteAddressBufferSRV,
        ByteAddressBufferUAV,
    };

    enum class ComputeBindingSemantic : uint8_t
    {
        Unknown,
        MeshVertices,
        MeshIndices,
        MeshDerivedFieldValues,
        Scratch,
    };

    struct ComputeBindingDesc
    {
        ComputeBindingKind kind{};
        ComputeBindingSemantic semantic{};
        uint32_t shader_register = 0;
        uint32_t register_space = 0;
        uint32_t descriptor_count = 1;
        uint32_t stride_bytes = 0;
    };

    struct ComputePipelineDesc
    {
        std::string name;
        wz::asset::AssetKey compute_shader{};
        std::vector<ComputeBindingDesc> bindings;
        uint32_t root_constant_dwords = 0;
        uint32_t thread_group_size_x = 1;
        uint32_t thread_group_size_y = 1;
        uint32_t thread_group_size_z = 1;
    };

    struct ComputePipelineData
    {
        std::string name;
        std::vector<ComputeBindingDesc> bindings;
        uint32_t root_constant_dwords = 0;
        uint32_t thread_group_size_x = 1;
        uint32_t thread_group_size_y = 1;
        uint32_t thread_group_size_z = 1;
        wz::asset::ResourceHandle compute_shader{};

        bool valid() const noexcept;
    };

    class ComputePipelineTable
    {
    public:
        ComputePipelineTable();

        wz::asset::ResourceHandle add(ComputePipelineData data);
        const ComputePipelineData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<ComputePipelineData> pipelines_;
        std::vector<uint32_t> epochs_;
    };
}
