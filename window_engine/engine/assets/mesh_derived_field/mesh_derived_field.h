#pragma once

// engine/assets/mesh_derived_field/mesh_derived_field.h

#include <asset/types.h>
#include <engine/assets/mesh/mesh.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    class MeshFieldComputeBackend;

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

    // Mesh data the engine extracts and binds as compute-kernel SRV inputs.
    // Inputs are bound in declared order at shader registers t0..tN-1,
    // register space 0; the packed output buffer is always bound at u0.
    enum class MeshComputeInput : uint8_t
    {
        Positions = 0,   // StructuredBuffer<float3>, always available
        Normals,         // StructuredBuffer<float3>, requires has_normals
        UV0,             // StructuredBuffer<float2>, requires has_uv0
        Indices,         // StructuredBuffer<uint>, triangle list
        Vertices,        // StructuredBuffer of interleaved MeshVertex
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

    struct GpuResidentFieldEntry
    {
        wz::asset::AssetKey field_key{};
        uint32_t channel_id = 0;
        wz::asset::ResourceHandle gpu_resource{};

        bool valid() const noexcept;
    };

    // GPU-resident copies of immutable mesh data, uploaded once per mesh
    // and reused by every compute mesh consumer: behavior compute kernels,
    // mesh_compute_field compilation, and future operator/wavelet kernels.
    // Keyed by the source mesh asset key — deliberately not by mesh field
    // visualization targets, since operator construction is not a
    // visualization feature. Zero-copy sharing with the renderer's mesh
    // buffers is a future concern, not handled here.
    struct GpuResidentMeshDataEntry
    {
        wz::asset::AssetKey mesh_key{};
        wz::asset::ResourceHandle positions{};   // StructuredBuffer<float3>
        wz::asset::ResourceHandle indices{};     // StructuredBuffer<uint>
        // Normals/uv0 slots land here when their compute refs do.
        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
        uint32_t triangle_count = 0;
    };

    class GpuResidentMeshDataTable
    {
    public:
        // Returns the entry for mesh_key, creating an empty one on first
        // use so callers can fill whichever buffer slots they need.
        // Returns nullptr for a default-constructed key.
        GpuResidentMeshDataEntry* find_or_add(wz::asset::AssetKey mesh_key);
        const GpuResidentMeshDataEntry* find(
            wz::asset::AssetKey mesh_key) const;
        void clear();
        void destroy(MeshFieldComputeBackend& compute);
        size_t size() const noexcept;

    private:
        std::vector<GpuResidentMeshDataEntry> entries_;
    };

    class GpuResidentFieldTable
    {
    public:
        bool add(GpuResidentFieldEntry entry);
        // Insert or swap the entry for (field_key, channel_id). A swapped-out
        // prior resource is released immediately, so callers must guarantee
        // the GPU is no longer executing work that binds it — the behavior
        // publish path satisfies this by fencing its copy on the same queue
        // before calling replace(). Steady-state per-frame refresh should use
        // wz::gpu::update_mesh_field_visualization_from_gpu_source() against
        // the handle returned by find() instead of swapping, which also keeps
        // handles captured by resolved renderables valid.
        bool replace(
            MeshFieldComputeBackend& compute,
            GpuResidentFieldEntry entry);
        wz::asset::ResourceHandle find(
            wz::asset::AssetKey field_key,
            uint32_t channel_id) const;
        void clear();
        void destroy(MeshFieldComputeBackend& compute);
        size_t size() const noexcept;

    private:
        std::vector<GpuResidentFieldEntry> entries_;
    };
}
