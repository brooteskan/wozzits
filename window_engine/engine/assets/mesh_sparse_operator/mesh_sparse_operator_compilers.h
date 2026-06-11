#pragma once

// engine/assets/mesh_sparse_operator/mesh_sparse_operator_compilers.h

#include <asset/compiler.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_mesh_sparse_operator_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        MeshSparseOperatorTable& mesh_sparse_operator_table,
        const EngineAssetCacheSettings& cache_settings);

    bool load_cached_mesh_sparse_operator(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshSparseOperatorData& data);
}
