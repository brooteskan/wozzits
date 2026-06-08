// src/engine/assets/mesh_derived_field_asset_module.cpp

#include <engine/assets/mesh_derived_field_asset_module.h>

#include <engine/assets/key_factories/mesh_derived_field.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

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

        if (!system_.register_asset(
                std::move(node),
                { desc.source_mesh.output }))
        {
            return MeshDerivedFieldAsset{ .output = field_key };
        }

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
