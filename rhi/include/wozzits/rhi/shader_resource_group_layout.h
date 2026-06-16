#pragma once

// wozzits/rhi/shader_resource_group_layout.h
//
// Slotted shader-resource-group contracts. The layout names the resources and
// constants once; bind-side data resolves those names to compact indices before
// the hot path.

#include <wozzits/rhi/constants_layout.h>
#include <wozzits/rhi/tag_registry.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace wz::rhi
{
    enum class ShaderStage : uint8_t { All, Vertex, Pixel, Compute };
    enum class DescriptorKind : uint8_t { StructuredBufferSRV, TextureSRV, Sampler, UAV };

    // Descriptor semantics are registered by name, exactly like render
    // programs. The adapter acquires a Tag per semantic and stamps it into the
    // SRG-local DescriptorBinding::semantic.
    inline constexpr size_t kMaxDescriptorSemantics = 128;
    using DescriptorSemanticRegistry = TagRegistry<kMaxDescriptorSemantics>;

    struct DescriptorBinding
    {
        DescriptorKind kind = DescriptorKind::StructuredBufferSRV;
        ShaderStage    visibility = ShaderStage::Pixel;

        // The OPEN identity within an SRG layout: which logical resource this
        // binding wants. A null Tag is a checkable bind-time error.
        Tag semantic{};

        uint32_t shader_register = 0;
        uint32_t register_space = 0;
        uint32_t descriptor_count = 1;
    };

    struct ShaderResourceGroupLayout
    {
        // Frequency/register-space slot. Convention: view=0, material=1,
        // object=2. Callers bind whole groups by this slot.
        uint32_t binding_slot = 0;

        std::vector<DescriptorBinding> descriptors;
        ConstantsLayout constants;

        [[nodiscard]] uint64_t hash() const noexcept
        {
            uint64_t h = 1469598103934665603ull;
            hash_combine(h, static_cast<uint64_t>(binding_slot));
            hash_combine(h, constants.hash());
            hash_combine(h, static_cast<uint64_t>(descriptors.size()));
            for (const DescriptorBinding& descriptor : descriptors) {
                hash_combine(h, static_cast<uint64_t>(descriptor.kind));
                hash_combine(h, static_cast<uint64_t>(descriptor.visibility));
                hash_combine(h, static_cast<uint64_t>(descriptor.semantic.index));
                hash_combine(h, static_cast<uint64_t>(descriptor.shader_register));
                hash_combine(h, static_cast<uint64_t>(descriptor.register_space));
                hash_combine(h, static_cast<uint64_t>(descriptor.descriptor_count));
            }
            return h;
        }

    private:
        static void hash_combine(uint64_t& h, uint64_t v) noexcept
        {
            h ^= v;
            h *= 1099511628211ull;
        }
    };

    [[nodiscard]] inline std::optional<uint32_t>
    find_descriptor_binding_index(
        const ShaderResourceGroupLayout& layout,
        Tag semantic) noexcept
    {
        if (!semantic.valid()) {
            return std::nullopt;
        }
        for (uint32_t i = 0;
             i < static_cast<uint32_t>(layout.descriptors.size());
             ++i)
        {
            if (layout.descriptors[i].semantic == semantic) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline const ShaderResourceGroupLayout*
    find_shader_resource_group_layout(
        std::span<const ShaderResourceGroupLayout> layouts,
        uint32_t binding_slot)
    {
        for (const ShaderResourceGroupLayout& layout : layouts) {
            if (layout.binding_slot == binding_slot) {
                return &layout;
            }
        }
        return nullptr;
    }

    [[nodiscard]] inline bool
    shader_resource_group_slots_are_unique(
        std::span<const ShaderResourceGroupLayout> layouts)
    {
        for (size_t i = 0; i < layouts.size(); ++i) {
            for (size_t j = i + 1; j < layouts.size(); ++j) {
                if (layouts[i].binding_slot == layouts[j].binding_slot) {
                    return false;
                }
            }
        }
        return true;
    }
}
