#pragma once

// engine/assets/key_factories/mesh_sparse_operator.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/mesh_sparse_operator_asset_module.h>
#include <engine/assets/schema_ids.h>

namespace wz::engine::assets
{
    // Identity: operator kind + domain in content_hash (recipes within the
    // one format); source mesh in deps_hash, so the operator rebuilds only
    // when the mesh or the compiler version changes. Same mesh with a
    // different kind yields a different key, cacheable side by side.
    [[nodiscard]] inline wz::asset::AssetKey
    make_mesh_sparse_operator_key(
        const wz::asset::AssetKey& source_mesh_key,
        const MeshSparseOperatorDesc& desc) noexcept
    {
        uint64_t h = kMeshSparseOperatorSchema.value;
        h = detail::mix64(h, static_cast<uint64_t>(desc.kind));
        h = detail::mix64(h, static_cast<uint64_t>(desc.domain));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(
                kMeshSparseOperatorSchema.value),
            .compiler_hash = detail::hash_u64(
                kMeshSparseOperatorCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_mesh_key),
        };
    }
}
