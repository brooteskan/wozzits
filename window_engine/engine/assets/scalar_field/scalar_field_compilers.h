#pragma once

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>
#include <engine/assets/scalar_field/scalar_field.h>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::assets::internal {

    // gpu_resources / rhi_resource_tracker are the shared-registry residency hook
    // (#197): when present, the compiler publishes the field as an rhi texture
    // resource. Null for a device-only library, which skips rhi residency.
    // RhiResourceTracker is defined in gpu_sparse_mesh_compilers.h (included).
    void register_scalar_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_field_table,
        const EngineAssetCacheSettings& cache_settings,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker
    );

    bool load_cached_scalar_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        ScalarFieldData& field);

}
