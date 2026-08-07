#pragma once

// wozzits/rhi/shader_resource_group.h
//
// Value-type shader resource group instance. It is built from an SRG layout,
// resolves Tags to indices at setup time, and stores bind-time state as indexed
// resources plus packed constant bytes.

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/shader_resource_group_layout.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace wz::rhi
{
    class ShaderResourceGroup
    {
    public:
        ShaderResourceGroup() = default;

        explicit ShaderResourceGroup(const ShaderResourceGroupLayout& layout)
        {
            reset(layout);
        }

        void reset(const ShaderResourceGroupLayout& layout)
        {
            binding_slot_ = layout.binding_slot;
            layout_hash_ = layout.hash();
            resource_semantics_.clear();
            resource_semantics_.reserve(layout.descriptors.size());
            for (const DescriptorBinding& descriptor : layout.descriptors) {
                resource_semantics_.push_back(descriptor.semantic);
            }
            resources_.assign(layout.descriptors.size(), GpuResourceHandle{});
            constants_layout_ = layout.constants;
            constant_bytes_.assign(layout.constants.byte_size(), uint8_t{ 0 });
        }

        [[nodiscard]] uint32_t binding_slot() const noexcept
        {
            return binding_slot_;
        }

        [[nodiscard]] uint64_t layout_hash() const noexcept
        {
            return layout_hash_;
        }

        [[nodiscard]] size_t resource_count() const noexcept
        {
            return resources_.size();
        }

        [[nodiscard]] size_t constant_byte_size() const noexcept
        {
            return constant_bytes_.size();
        }

        [[nodiscard]] std::optional<uint32_t>
        resolve_resource_index(Tag semantic) const noexcept
        {
            if (!semantic.valid()) {
                return std::nullopt;
            }
            for (uint32_t i = 0;
                 i < static_cast<uint32_t>(resource_semantics_.size());
                 ++i)
            {
                if (resource_semantics_[i] == semantic) {
                    return i;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<uint32_t>
        set(Tag semantic, GpuResourceHandle handle)
        {
            const std::optional<uint32_t> index =
                resolve_resource_index(semantic);
            if (!index || !set(*index, handle)) {
                return std::nullopt;
            }
            return index;
        }

        bool set(uint32_t index, GpuResourceHandle handle)
        {
            if (index >= resources_.size()) {
                return false;
            }
            resources_[index] = handle;
            return true;
        }

        [[nodiscard]] GpuResourceHandle resource(uint32_t index) const noexcept
        {
            return index < resources_.size() ? resources_[index]
                                             : GpuResourceHandle{};
        }

        [[nodiscard]] std::span<const GpuResourceHandle>
        resources() const noexcept
        {
            return resources_;
        }

        [[nodiscard]] std::optional<ConstantInterval>
        set_constant(Tag semantic, std::span<const uint8_t> bytes)
        {
            const std::optional<ConstantInterval> interval =
                constants_layout_.find(semantic);
            if (!interval || !set_constant(*interval, bytes)) {
                return std::nullopt;
            }
            return interval;
        }

        bool set_constant(
            ConstantInterval interval,
            std::span<const uint8_t> bytes)
        {
            if (!interval.valid()
                || bytes.size() != interval.byte_size
                || interval.byte_offset > constant_bytes_.size()
                || bytes.size() > constant_bytes_.size() - interval.byte_offset)
            {
                return false;
            }

            std::memcpy(
                constant_bytes_.data() + interval.byte_offset,
                bytes.data(),
                bytes.size());
            return true;
        }

        [[nodiscard]] std::span<const uint8_t>
        constant_bytes() const noexcept
        {
            return constant_bytes_;
        }

        [[nodiscard]] bool satisfies(
            const ShaderResourceGroupLayout& layout) const noexcept
        {
            if (binding_slot_ != layout.binding_slot
                || layout_hash_ != layout.hash()
                || resources_.size() != layout.descriptors.size()
                || constant_bytes_.size() != layout.constants.byte_size())
            {
                return false;
            }

            for (size_t i = 0; i < layout.descriptors.size(); ++i) {
                const Tag required = layout.descriptors[i].semantic;
                if (!required.valid()
                    || i >= resource_semantics_.size()
                    || !(resource_semantics_[i] == required)
                    || !resources_[i].valid())
                {
                    return false;
                }
            }
            return true;
        }

    private:
        uint32_t binding_slot_ = 0;
        uint64_t layout_hash_ = 0;
        std::vector<Tag> resource_semantics_;
        std::vector<GpuResourceHandle> resources_;
        ConstantsLayout constants_layout_;
        std::vector<uint8_t> constant_bytes_;
    };
}
