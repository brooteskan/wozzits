#pragma once

// engine/assets/mesh_derived_field/mesh_derived_field_compilers.h

#include <asset/compiler.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/compute_pipeline/compute_pipeline.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_derived_field/mesh_field_compute.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_mesh_derived_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshFieldComputeBackend& mesh_field_compute,
        MeshTable& mesh_table,
        ComputePipelineTable& compute_pipeline_table,
        MeshDerivedFieldTable& mesh_derived_field_table,
        MeshSparseOperatorTable& mesh_sparse_operator_table,
        GpuResidentFieldTable& gpu_resident_field_table,
        GpuResidentMeshDataTable& gpu_resident_mesh_data_table,
        const EngineAssetCacheSettings& cache_settings);

    bool load_cached_mesh_derived_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshDerivedFieldData& field);

    bool load_cached_mesh_wavelet_analysis_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshDerivedFieldData& field);

    bool load_cached_mesh_compute_derived_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshDerivedFieldData& field);
}
