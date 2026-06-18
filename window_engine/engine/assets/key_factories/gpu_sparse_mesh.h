#pragma once

// engine/assets/key_factories/gpu_sparse_mesh.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/gpu_sparse_mesh_asset_module.h>
#include <engine/assets/schema_ids.h>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_gpu_sparse_mesh_key(
        const wz::asset::AssetKey& source_mesh_key,
        const GpuSparseMeshDesc& desc) noexcept
    {
        uint64_t h = kGpuSparseMeshFromMeshSchema.value;
        h = detail::mix64(h, static_cast<uint64_t>(desc.operator_kind));
        h = detail::mix64(h, static_cast<uint64_t>(desc.operator_domain));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kGpuSparseMeshFromMeshSchema.value),
            .compiler_hash = detail::hash_u64(kGpuSparseMeshCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_mesh_key),
        };
    }
}
