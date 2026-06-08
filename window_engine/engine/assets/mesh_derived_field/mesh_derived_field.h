#pragma once

// engine/assets/mesh_derived_field/mesh_derived_field.h

#include <asset/types.h>
#include <engine/assets/mesh/mesh.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    namespace MeshWaveletChannelID
    {
        inline constexpr uint32_t kPositionEnergyBase = 0x1000u;
        inline constexpr uint32_t kNormalEnergyBase = 0x1100u;
        inline constexpr uint32_t kDetailCost = 0x1200u;
    }

    enum class MeshDerivedFieldDomain : uint8_t
    {
        Vertex,
        Edge,
        Face,
        Corner,
    };

    enum class MeshDerivedFieldValueType : uint8_t
    {
        Float1,
        Float2,
        Float3,
        Float4,
        UInt1,
    };

    struct MeshDerivedFieldChannel
    {
        uint32_t channel_id = 0;
        MeshDerivedFieldValueType value_type =
            MeshDerivedFieldValueType::Float1;
        uint32_t byte_offset = 0;
        uint32_t byte_count = 0;
    };

    struct MeshDerivedFieldData
    {
        wz::asset::AssetKey source_mesh_key{};
        wz::asset::Hash source_topology_hash{};
        MeshDerivedFieldDomain domain = MeshDerivedFieldDomain::Vertex;
        uint32_t element_count = 0;
        std::vector<MeshDerivedFieldChannel> channels;
        std::vector<std::byte> values;

        bool valid() const noexcept;
    };

    struct MeshDerivedFieldChannelDesc
    {
        uint32_t channel_id = 0;
        MeshDerivedFieldValueType value_type =
            MeshDerivedFieldValueType::Float1;
        std::vector<std::byte> values;
    };

    [[nodiscard]] uint32_t mesh_derived_field_value_stride(
        MeshDerivedFieldValueType value_type) noexcept;

    [[nodiscard]] wz::asset::Hash compute_mesh_topology_hash(
        const MeshData& mesh) noexcept;

    [[nodiscard]] uint32_t mesh_domain_element_count(
        const MeshData& mesh,
        MeshDerivedFieldDomain domain);

    class MeshDerivedFieldTable
    {
    public:
        MeshDerivedFieldTable();

        wz::asset::ResourceHandle add(MeshDerivedFieldData field);
        const MeshDerivedFieldData* get(
            wz::asset::ResourceHandle handle) const;
        MeshDerivedFieldData* get_mutable_for_tests(
            wz::asset::ResourceHandle handle);

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            MeshDerivedFieldData field;
        };

        std::vector<Slot> slots_;
    };
}
