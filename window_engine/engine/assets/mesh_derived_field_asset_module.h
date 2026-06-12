#pragma once

// engine/assets/mesh_derived_field_asset_module.h

#include <asset/system.h>
#include <engine/assets/compute_pipeline_asset_module.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_sparse_operator_asset_module.h>

#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct ExplicitMeshDerivedFieldDesc
    {
        std::string name;
        MeshAsset source_mesh;
        MeshDerivedFieldDomain domain = MeshDerivedFieldDomain::Vertex;
        uint32_t element_count = 0;
        std::vector<MeshDerivedFieldChannelDesc> channels;
    };

    struct MeshWaveletAnalysisDesc
    {
        std::string name;
        MeshAsset source_mesh;
        ComputePipelineAsset compute_pipeline{};
        uint32_t scale_count = 3;
        float lambda_max_estimate = 2.0f;
        float gamma = 1.0f;
    };

    enum class BuiltinMeshDerivedFieldSourceKind : uint8_t
    {
        Constant = 0,
        PositionGradient,
        VertexIndexGradient,
        TriangleCornerCount,
        VertexArea,
        TriangleArea,
        MeanEdgeLength,
        InverseAreaDensity,
        LogDensity,
    };

    enum class BuiltinMeshDerivedFieldComponent : uint8_t
    {
        X = 0,
        Y,
        Z,
    };

    struct BuiltinMeshDerivedFieldDesc
    {
        std::string name;
        MeshAsset source_mesh;
        MeshDerivedFieldDomain domain = MeshDerivedFieldDomain::Vertex;
        uint32_t channel_id = 0x2000u;
        MeshDerivedFieldValueType value_type =
            MeshDerivedFieldValueType::Float1;
        BuiltinMeshDerivedFieldSourceKind source_kind =
            BuiltinMeshDerivedFieldSourceKind::PositionGradient;
        BuiltinMeshDerivedFieldComponent component =
            BuiltinMeshDerivedFieldComponent::Y;
        bool normalize = true;
        float constant_value = 0.5f;
    };

    struct BehaviorFieldPlaceholderDesc
    {
        std::string name;
        MeshAsset source_mesh;
        MeshDerivedFieldDomain domain = MeshDerivedFieldDomain::Vertex;
        uint32_t channel_id = 0;
    };

    // Project-authored compute kernel compiled into a cached field asset.
    // GPU compute accelerates the compile step only; nothing here runs per
    // frame. The dispatch root-constant layout is three engine-filled dwords
    // (vertex_count, index_count, triangle_count) followed by the authored
    // root_constants. channels declares the output layout only; channel
    // payload bytes are produced by the kernel, so each
    // MeshDerivedFieldChannelDesc::values must be empty.
    struct MeshComputeDerivedFieldDesc
    {
        std::string name;
        MeshAsset source_mesh;
        ComputePipelineAsset compute_pipeline{};
        MeshDerivedFieldDomain domain = MeshDerivedFieldDomain::Vertex;
        std::vector<MeshDerivedFieldChannelDesc> channels;
        std::vector<MeshComputeInput> inputs;
        std::vector<uint32_t> root_constants;
    };

    struct MeshDerivedFieldAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    enum class MeshSparseApplyMode : uint8_t
    {
        Residual = 0,
    };

    enum class MeshSparseDiffusionMode : uint8_t
    {
        Smooth = 0,
        DiffusionStep,
    };

    struct MeshSparseApplyFieldDesc
    {
        std::string name;
        MeshAsset source_mesh;
        MeshSparseOperatorAsset sparse_operator{};
        MeshDerivedFieldAsset input_field{};
        uint32_t input_channel_id = 0;
        uint32_t output_channel_id = 0;
        MeshSparseApplyMode apply_mode = MeshSparseApplyMode::Residual;
    };

    struct MeshSparseDiffusionBandsDesc
    {
        std::string name;
        MeshAsset source_mesh;
        MeshSparseOperatorAsset sparse_operator{};
        MeshDerivedFieldAsset input_field{};
        uint32_t input_channel_id = 0;
        uint32_t output_base_channel_id = 0;
        uint32_t band_count = 1;
        uint32_t iterations_per_band = 1;
        MeshSparseDiffusionMode mode = MeshSparseDiffusionMode::Smooth;
        float tau = 1.0f;
    };

    struct MeshDerivedFieldHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class MeshDerivedFieldAssetModule
    {
    public:
        explicit MeshDerivedFieldAssetModule(
            wz::asset::AssetSystem& system,
            MeshDerivedFieldTable& table);

        [[nodiscard]] MeshDerivedFieldAsset create_explicit_field(
            const ExplicitMeshDerivedFieldDesc& desc);

        [[nodiscard]] MeshDerivedFieldAsset create_wavelet_analysis(
            const MeshWaveletAnalysisDesc& desc);

        [[nodiscard]] MeshDerivedFieldAsset create_builtin_field(
            const BuiltinMeshDerivedFieldDesc& desc);

        [[nodiscard]] MeshDerivedFieldAsset create_behavior_field_placeholder(
            const BehaviorFieldPlaceholderDesc& desc);

        [[nodiscard]] MeshDerivedFieldAsset create_compute_derived_field(
            const MeshComputeDerivedFieldDesc& desc);

        [[nodiscard]] MeshDerivedFieldAsset create_sparse_apply_field(
            const MeshSparseApplyFieldDesc& desc);

        [[nodiscard]] MeshDerivedFieldAsset create_sparse_diffusion_bands(
            const MeshSparseDiffusionBandsDesc& desc);

        [[nodiscard]] MeshDerivedFieldHandle get_mesh_derived_field(
            const MeshDerivedFieldAsset& asset) const;

        [[nodiscard]] const MeshDerivedFieldData* get_mesh_derived_field_data(
            MeshDerivedFieldHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        MeshDerivedFieldTable& table_;
    };
}
