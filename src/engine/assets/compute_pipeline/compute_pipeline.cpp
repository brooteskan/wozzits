#include <engine/assets/compute_pipeline/compute_pipeline.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <tuple>
#include <utility>

namespace wz::engine::assets
{
    namespace
    {
        bool is_structured(ComputeBindingKind kind) noexcept
        {
            return kind == ComputeBindingKind::StructuredBufferSRV
                || kind == ComputeBindingKind::StructuredBufferUAV;
        }

        uint8_t register_class(ComputeBindingKind kind) noexcept
        {
            switch (kind) {
            case ComputeBindingKind::StructuredBufferSRV:
            case ComputeBindingKind::ByteAddressBufferSRV:
                return 0;
            case ComputeBindingKind::StructuredBufferUAV:
            case ComputeBindingKind::ByteAddressBufferUAV:
                return 1;
            }
            return 0;
        }

        bool has_duplicate_binding_slots(
            const std::vector<ComputeBindingDesc>& bindings)
        {
            std::vector<std::tuple<uint8_t, uint32_t, uint32_t>> slots;
            slots.reserve(bindings.size());
            for (const ComputeBindingDesc& binding : bindings) {
                slots.emplace_back(
                    register_class(binding.kind),
                    binding.register_space,
                    binding.shader_register);
            }
            std::sort(slots.begin(), slots.end());
            return std::adjacent_find(slots.begin(), slots.end())
                != slots.end();
        }
    }

    bool ComputePipelineData::valid() const noexcept
    {
        if (name.empty()
            || !compute_shader.valid()
            || thread_group_size_x == 0u
            || thread_group_size_y == 0u
            || thread_group_size_z == 0u)
        {
            return false;
        }

        for (const ComputeBindingDesc& binding : bindings) {
            if (binding.descriptor_count == 0u) {
                return false;
            }
            if (is_structured(binding.kind) && binding.stride_bytes == 0u) {
                return false;
            }
        }

        return !has_duplicate_binding_slots(bindings);
    }

    ComputePipelineTable::ComputePipelineTable()
    {
        pipelines_.push_back({});
        epochs_.push_back(0);
    }

    wz::asset::ResourceHandle ComputePipelineTable::add(
        ComputePipelineData data)
    {
        if (!data.valid())
            return {};

        const uint32_t id = static_cast<uint32_t>(pipelines_.size());
        pipelines_.push_back(std::move(data));
        epochs_.push_back(1);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = 1,
            .type = kAssetTypeComputePipeline,
        };
    }

    const ComputePipelineData* ComputePipelineTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != kAssetTypeComputePipeline)
            return nullptr;

        if (handle.id >= pipelines_.size())
            return nullptr;

        if (epochs_[handle.id] != handle.epoch)
            return nullptr;

        return &pipelines_[handle.id];
    }

    void ComputePipelineTable::destroy()
    {
        pipelines_.clear();
        epochs_.clear();

        pipelines_.push_back({});
        epochs_.push_back(0);
    }
}
