#pragma once

// engine/assets/vector_field/vector_field.h
//
// Runtime data types and table for vector field assets.

#include <asset/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    enum class VectorFieldFormat : uint8_t
    {
        Float32,
    };

    enum class VectorFieldDomainKind : uint8_t
    {
        Unknown,
        Spatial1D,
        Spatial2D,
        Lookup1D,
        Lookup2D,
        BakedComputation,
    };

    enum class VectorFieldSampleLayout : uint8_t
    {
        TexelCentered,
    };

    enum class VectorFieldOrigin : uint8_t
    {
        TopLeft,
    };

    struct VectorFieldChannelDesc
    {
        std::string name;
    };

    // Immutable CPU-side sampled field with named channels and a fixed
    // component count per channel. Storage order is sample-major, then channel,
    // then component:
    //
    //   (((z * height + y) * width + x) * channel_count + c)
    //       * components_per_channel + k
    //
    // V1 supports Float32 storage and component counts 2, 3, or 4. The 1x1
    // base case remains represented by ScalarFieldData until the shared Field
    // representation lands.
    struct VectorFieldData
    {
        uint32_t width = 0;
        uint32_t height = 1;
        uint32_t depth = 1;

        uint32_t components_per_channel = 0;
        std::vector<VectorFieldChannelDesc> channels;

        VectorFieldFormat format = VectorFieldFormat::Float32;
        VectorFieldDomainKind domain_kind = VectorFieldDomainKind::Unknown;
        VectorFieldSampleLayout layout = VectorFieldSampleLayout::TexelCentered;
        VectorFieldOrigin origin = VectorFieldOrigin::TopLeft;

        std::vector<float> min_values;
        std::vector<float> max_values;
        std::vector<float> values;

        uint32_t sample_count() const noexcept
        {
            return width * height * depth;
        }

        uint32_t channel_count() const noexcept
        {
            return static_cast<uint32_t>(channels.size());
        }

        uint32_t value_count() const noexcept
        {
            return sample_count() * channel_count() * components_per_channel;
        }

        bool valid() const noexcept
        {
            return width > 0
                && height > 0
                && depth > 0
                && channel_count() > 0
                && components_per_channel >= 2
                && components_per_channel <= 4
                && values.size() == static_cast<size_t>(value_count())
                && min_values.size()
                    == static_cast<size_t>(
                        channel_count() * components_per_channel)
                && max_values.size() == min_values.size();
        }

        size_t value_index(
            uint32_t x,
            uint32_t y,
            uint32_t z,
            uint32_t channel,
            uint32_t component) const noexcept
        {
            const size_t sample =
                static_cast<size_t>(x)
                + static_cast<size_t>(y) * width
                + static_cast<size_t>(z) * width * height;
            return (sample * channel_count() + channel)
                * components_per_channel + component;
        }

        float at(
            uint32_t x,
            uint32_t y,
            uint32_t z,
            uint32_t channel,
            uint32_t component) const
        {
            return values[value_index(x, y, z, channel, component)];
        }
    };

    struct VectorFieldCompileDesc
    {
        uint32_t width = 0;
        uint32_t height = 1;
        uint32_t depth = 1;

        uint32_t components_per_channel = 0;
        std::vector<VectorFieldChannelDesc> channels;

        VectorFieldFormat format = VectorFieldFormat::Float32;
        VectorFieldDomainKind domain_kind = VectorFieldDomainKind::Spatial2D;
    };

    class VectorFieldTable
    {
    public:
        VectorFieldTable();

        wz::asset::ResourceHandle add(VectorFieldData field);
        const VectorFieldData* get(wz::asset::ResourceHandle handle) const;
        VectorFieldData* get_mutable_for_tests(wz::asset::ResourceHandle handle);
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            VectorFieldData field;
        };

        std::vector<Slot> slots_;
    };

} // namespace wz::engine::assets
