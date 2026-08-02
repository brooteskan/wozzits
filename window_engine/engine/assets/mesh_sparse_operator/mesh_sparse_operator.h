#pragma once

// engine/assets/mesh_sparse_operator/mesh_sparse_operator.h
//
// Second derived-mesh-data format: a CSR sparse operator over a mesh
// domain. MeshDerivedFieldData holds per-element values; this format holds
// relationships/operators between elements, so compute shaders consume a
// prebuilt sparse graph operator instead of rediscovering topology every
// dispatch. Operator kinds are recipes within this one format, selected by
// compiler argument — not new asset types (see the derived-data design
// rule on issue #150).

#include <asset/types.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    // Domain whose elements the operator's rows index. Shares the
    // MeshDerivedFieldDomain vocabulary so fields and the operators that
    // act on them speak the same language.
    using MeshOperatorDomain = MeshDerivedFieldDomain;

    enum class MeshSparseOperatorKind : uint8_t
    {
        // w_ij = 1 / degree(i) over the selected domain adjacency graph;
        // rows sum to 1 (isolated elements have empty rows). vertex_mass
        // is all 1.
        UniformAdjacency = 0,
        // Back-compat name for existing authored scenes and tests.
        UniformVertexLaplacian = 0,
        // v1: CotangentVertexLaplacian (degenerate-triangle, boundary,
        // area/mass, and sign-convention handling); later adjacency,
        // stiffness, ...
    };

    // What the weights array means. This is part of the format so a
    // diffusion operator, a graph Laplacian, and a cotangent stiffness
    // matrix cannot be silently confused by consumers.
    enum class MeshSparseOperatorValueConvention : uint8_t
    {
        // Rows store neighbor weights w_ij only (no diagonal). Consumers
        // apply the Laplacian as:
        //   Lf[i] = f[i] - sum_j w_ij * f[j]
        // For UniformVertexLaplacian each row sums to 1, so L annihilates
        // constant fields.
        NeighborWeights = 0,
        // Rows store actual matrix entries including the diagonal; apply
        // as a plain sparse matrix-vector product. No v0 kind produces
        // this yet.
        FullMatrixEntries,
    };

    // ─── Descriptor range checks ──────────────────────────────────────────────────
    //
    // kind, domain and value_convention are stored in the sparse-operator disk
    // cache as raw uint8 and were static_cast straight back with no range
    // check, so a flipped byte produced an out-of-range enum that every other
    // check in the loader missed -- magic, format version, compiler version and
    // the stored key all cover different byte regions. value_convention decides
    // how a consumer APPLIES the weights, which is the whole point of storing
    // it. Same shape as the scalar-field descriptors (B1-C4). The domain
    // predicate lives with MeshDerivedFieldDomain, whose vocabulary
    // MeshOperatorDomain aliases.
    //
    // WHEN YOU APPEND AN ENUMERATOR, UPDATE THE MATCHING PREDICATE. They live
    // beside the enums so the pairing is visible at the point of change.
    // Under-accepting is the safe direction: an entry carrying an enumerator
    // this build does not know is rejected, and the asset recompiles.

    [[nodiscard]] constexpr bool valid_mesh_sparse_operator_kind(
        uint8_t v) noexcept
    {
        // Both enumerators name ordinal 0; UniformAdjacency is the current
        // spelling and UniformVertexLaplacian the back-compat one.
        return v <= static_cast<uint8_t>(
            MeshSparseOperatorKind::UniformAdjacency);
    }

    [[nodiscard]] constexpr bool valid_mesh_sparse_operator_value_convention(
        uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(
            MeshSparseOperatorValueConvention::FullMatrixEntries);
    }

    struct MeshSparseOperatorData
    {
        wz::asset::AssetKey source_mesh_key{};
        wz::asset::Hash source_topology_hash{};
        MeshSparseOperatorKind kind =
            MeshSparseOperatorKind::UniformVertexLaplacian;
        MeshOperatorDomain domain = MeshOperatorDomain::Vertex;
        MeshSparseOperatorValueConvention value_convention =
            MeshSparseOperatorValueConvention::NeighborWeights;

        // CSR storage. row_count is the domain element count (the vertex
        // count for Vertex-domain operators). Within each row, col_indices
        // are strictly ascending — required for deterministic hashes, disk
        // cache bytes, and reproducible tests.
        uint32_t row_count = 0;
        uint32_t nonzero_count = 0;
        std::vector<uint32_t> row_offsets;   // row_count + 1
        std::vector<uint32_t> col_indices;   // nonzero_count
        std::vector<float>    weights;       // nonzero_count

        // Optional lumped mass diagonal companion (row_count entries, or
        // empty for operators that have none). All 1 for
        // UniformVertexLaplacian.
        std::vector<float> vertex_mass;

        bool valid() const noexcept;
    };

    // GPU-resident copies of a compiled sparse operator's CSR buffers,
    // uploaded once per operator and reused by every compute consumer —
    // re-uploading ~hundreds of MB of CSR data per dispatch would defeat
    // the operator at the target mesh scale. Keyed by the operator asset
    // key (which already encodes source mesh + kind + domain).
    struct GpuResidentSparseOperatorEntry
    {
        wz::asset::AssetKey operator_key{};
        wz::asset::ResourceHandle row_offsets{};   // uint, row_count + 1
        wz::asset::ResourceHandle col_indices{};   // uint, nonzero_count
        wz::asset::ResourceHandle weights{};       // float, nonzero_count
        wz::asset::ResourceHandle vertex_mass{};   // float, row_count
        uint32_t row_count = 0;
        uint32_t nonzero_count = 0;
    };

    class GpuResidentSparseOperatorTable
    {
    public:
        // Returns the entry for operator_key, creating an empty one on
        // first use so callers can fill the buffer slots they upload.
        // Returns nullptr for a default-constructed key.
        GpuResidentSparseOperatorEntry* find_or_add(
            wz::asset::AssetKey operator_key);
        const GpuResidentSparseOperatorEntry* find(
            wz::asset::AssetKey operator_key) const;
        // No buffer-dropping clear(): entries own GPU buffers, so the only
        // way to empty the table is destroy(), which releases them.
        void destroy(MeshFieldComputeBackend& compute);
        size_t size() const noexcept;

    private:
        std::vector<GpuResidentSparseOperatorEntry> entries_;
    };

    class MeshSparseOperatorTable
    {
    public:
        MeshSparseOperatorTable();

        wz::asset::ResourceHandle add(MeshSparseOperatorData data);
        const MeshSparseOperatorData* get(
            wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            MeshSparseOperatorData data;
        };

        std::vector<Slot> slots_;
    };
}
