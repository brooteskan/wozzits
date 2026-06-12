// src/engine/assets/mesh_derived_field_asset_module.cpp

#include <engine/assets/mesh_derived_field_asset_module.h>

#include <engine/assets/key_factories/mesh_derived_field.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <cmath>

namespace wz::engine::assets
{
    MeshDerivedFieldAssetModule::MeshDerivedFieldAssetModule(
        wz::asset::AssetSystem& system,
        MeshDerivedFieldTable& table)
        : system_(system)
        , table_(table)
    {
    }

    MeshDerivedFieldAsset MeshDerivedFieldAssetModule::create_explicit_field(
        const ExplicitMeshDerivedFieldDesc& desc)
    {
        if (!desc.source_mesh.valid()
            || desc.element_count == 0u
            || desc.channels.empty())
        {
            return {};
        }

        const wz::asset::AssetKey field_key =
            make_explicit_mesh_derived_field_key(
                desc.source_mesh.output,
                desc);

        wz::asset::AssetNode node{};
        node.key = field_key;
        node.type = kAssetTypeMeshDerivedField;
        node.schema = kMeshDerivedFieldExplicitSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        if (!system_.register_asset(
                std::move(node),
                { desc.source_mesh.output }))
        {
            return MeshDerivedFieldAsset{ .output = field_key };
        }

        return MeshDerivedFieldAsset{ .output = field_key };
    }

    MeshDerivedFieldAsset
    MeshDerivedFieldAssetModule::create_wavelet_analysis(
        const MeshWaveletAnalysisDesc& desc)
    {
        if (!desc.source_mesh.valid()
            || desc.scale_count == 0u
            || desc.lambda_max_estimate <= 0.0f
            || desc.gamma <= 0.0f)
        {
            return {};
        }

        const wz::asset::AssetKey field_key =
            make_mesh_wavelet_analysis_field_key(
                desc.source_mesh.output,
                desc);

        wz::asset::AssetNode node{};
        node.key = field_key;
        node.type = kAssetTypeMeshDerivedField;
        node.schema = kMeshWaveletAnalysisSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        const std::vector<wz::asset::AssetKey> deps{
            desc.source_mesh.output,
            desc.compute_pipeline.valid()
                ? desc.compute_pipeline.key
                : wz::asset::AssetKey{},
        };

        if (!system_.register_asset(std::move(node), deps))
        {
            return MeshDerivedFieldAsset{ .output = field_key };
        }

        return MeshDerivedFieldAsset{ .output = field_key };
    }

    MeshDerivedFieldAsset MeshDerivedFieldAssetModule::create_builtin_field(
        const BuiltinMeshDerivedFieldDesc& desc)
    {
        if (!desc.source_mesh.valid()
            || (desc.domain != MeshDerivedFieldDomain::Vertex
                && desc.domain != MeshDerivedFieldDomain::Face)
            || desc.channel_id == 0u
            || desc.value_type != MeshDerivedFieldValueType::Float1)
        {
            return {};
        }

        const wz::asset::AssetKey field_key =
            make_builtin_mesh_derived_field_key(
                desc.source_mesh.output,
                desc);

        wz::asset::AssetNode node{};
        node.key = field_key;
        node.type = kAssetTypeMeshDerivedField;
        node.schema = kBuiltinMeshDerivedFieldSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        if (!system_.register_asset(
                std::move(node),
                { desc.source_mesh.output }))
        {
            return MeshDerivedFieldAsset{ .output = field_key };
        }

        return MeshDerivedFieldAsset{ .output = field_key };
    }

    MeshDerivedFieldAsset
    MeshDerivedFieldAssetModule::create_behavior_field_placeholder(
        const BehaviorFieldPlaceholderDesc& desc)
    {
        if (!desc.source_mesh.valid() || desc.channel_id == 0u) {
            return {};
        }

        const wz::asset::AssetKey field_key =
            make_behavior_field_placeholder_key(
                desc.source_mesh.output,
                desc);

        wz::asset::AssetNode node{};
        node.key = field_key;
        node.type = kAssetTypeMeshDerivedField;
        node.schema = kBehaviorFieldPlaceholderSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        // register_asset() returns false when the key is already registered;
        // re-materializing the same scene is expected to hit that, so treat
        // it as success and hand back the existing key.
        (void)system_.register_asset(
            std::move(node),
            { desc.source_mesh.output });

        return MeshDerivedFieldAsset{ .output = field_key };
    }

    namespace
    {
        bool compute_derived_field_desc_valid(
            const MeshComputeDerivedFieldDesc& desc)
        {
            if (!desc.source_mesh.valid()
                || !desc.compute_pipeline.valid()
                || desc.channels.empty())
            {
                return false;
            }
            for (size_t i = 0; i < desc.channels.size(); ++i) {
                if (desc.channels[i].channel_id == 0u
                    || !desc.channels[i].values.empty())
                {
                    return false;
                }
                for (size_t j = i + 1; j < desc.channels.size(); ++j) {
                    if (desc.channels[i].channel_id
                        == desc.channels[j].channel_id)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
    }

    MeshDerivedFieldAsset
    MeshDerivedFieldAssetModule::create_compute_derived_field(
        const MeshComputeDerivedFieldDesc& desc)
    {
        if (!compute_derived_field_desc_valid(desc)) {
            return {};
        }

        const wz::asset::AssetKey field_key =
            make_mesh_compute_derived_field_key(
                desc.source_mesh.output,
                desc);

        wz::asset::AssetNode node{};
        node.key = field_key;
        node.type = kAssetTypeMeshDerivedField;
        node.schema = kMeshComputeDerivedFieldSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        // register_asset() returns false when the key is already registered;
        // re-materializing the same scene is expected to hit that, so treat
        // it as success and hand back the existing key.
        (void)system_.register_asset(
            std::move(node),
            { desc.source_mesh.output, desc.compute_pipeline.key });

        return MeshDerivedFieldAsset{ .output = field_key };
    }

    MeshDerivedFieldAsset
    MeshDerivedFieldAssetModule::create_sparse_apply_field(
        const MeshSparseApplyFieldDesc& desc)
    {
        if (!desc.source_mesh.valid()
            || !desc.sparse_operator.valid()
            || !desc.input_field.valid()
            || desc.input_channel_id == 0u
            || desc.output_channel_id == 0u)
        {
            return {};
        }

        const wz::asset::AssetKey field_key =
            make_mesh_sparse_apply_field_key(
                desc.source_mesh.output,
                desc);

        wz::asset::AssetNode node{};
        node.key = field_key;
        node.type = kAssetTypeMeshDerivedField;
        node.schema = kMeshSparseApplyFieldSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        (void)system_.register_asset(
            std::move(node),
            {
                desc.source_mesh.output,
                desc.input_field.output,
                desc.sparse_operator.output,
            });

        return MeshDerivedFieldAsset{ .output = field_key };
    }

    MeshDerivedFieldAsset
    MeshDerivedFieldAssetModule::create_sparse_diffusion_bands(
        const MeshSparseDiffusionBandsDesc& desc)
    {
        if (!desc.source_mesh.valid()
            || !desc.sparse_operator.valid()
            || !desc.input_field.valid()
            || desc.input_channel_id == 0u
            || desc.output_base_channel_id == 0u
            || desc.band_count == 0u
            || desc.iterations_per_band == 0u
            || !std::isfinite(desc.tau)
            || desc.tau < 0.0f)
        {
            return {};
        }

        const wz::asset::AssetKey field_key =
            make_mesh_sparse_diffusion_bands_key(
                desc.source_mesh.output,
                desc);

        wz::asset::AssetNode node{};
        node.key = field_key;
        node.type = kAssetTypeMeshDerivedField;
        node.schema = kMeshSparseDiffusionBandsSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        (void)system_.register_asset(
            std::move(node),
            {
                desc.source_mesh.output,
                desc.input_field.output,
                desc.sparse_operator.output,
            });

        return MeshDerivedFieldAsset{ .output = field_key };
    }

    MeshDerivedFieldAsset
    MeshDerivedFieldAssetModule::create_field_level_mask(
        const MeshFieldLevelMaskDesc& desc)
    {
        if (!desc.source_mesh.valid()
            || !desc.input_field.valid()
            || (desc.domain != MeshDerivedFieldDomain::Vertex
                && desc.domain != MeshDerivedFieldDomain::Face)
            || desc.regions.empty())
        {
            return {};
        }

        for (const MeshFieldLevelMaskRegionDesc& region : desc.regions) {
            if (region.input_channel_id == 0u
                || region.output_channel_id == 0u
                || !std::isfinite(region.min_value)
                || !std::isfinite(region.max_value))
            {
                return {};
            }
        }

        const wz::asset::AssetKey field_key =
            make_mesh_field_level_mask_key(
                desc.source_mesh.output,
                desc);

        wz::asset::AssetNode node{};
        node.key = field_key;
        node.type = kAssetTypeMeshDerivedField;
        node.schema = kMeshFieldLevelMaskSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        (void)system_.register_asset(
            std::move(node),
            {
                desc.source_mesh.output,
                desc.input_field.output,
            });

        return MeshDerivedFieldAsset{ .output = field_key };
    }

    MeshDerivedFieldHandle
    MeshDerivedFieldAssetModule::get_mesh_derived_field(
        const MeshDerivedFieldAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        MeshDerivedFieldHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        return out;
    }

    const MeshDerivedFieldData*
    MeshDerivedFieldAssetModule::get_mesh_derived_field_data(
        MeshDerivedFieldHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        return table_.get(handle.handle);
    }
}
