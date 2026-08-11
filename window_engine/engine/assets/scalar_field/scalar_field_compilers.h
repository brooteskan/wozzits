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

    // Publish a scalar field as an rhi texture resource (mip pyramid + the
    // deterministic "field_texture" identity) — the GPU-residency side effect the
    // compiler runs in its finalize path. Exposed so the disk-cache provider can
    // re-publish residency for a field it serves STRAIGHT from cache (bypassing
    // the compiler), matching a fresh compile — otherwise a cache-served height
    // field is CPU-only and the clipmap's scalar_field_texture binding renders
    // "not resident" (issue #295). No-op semantics match the compiler: only a
    // valid 2D R32F field (depth 1) is uploaded.
    void publish_scalar_field_residency(
        const wz::asset::AssetKey& key,
        const ScalarFieldData& field,
        wz::rhi::GpuResourceRegistry& gpu_resources,
        const RhiResourceTracker& rhi_resource_tracker,
        wz::Logger& logger);

    // ── Height mip pyramid (#210) ─────────────────────────────────────────────
    //
    // One level of a CPU-built box-filter mip pyramid for the resident height
    // texture. `values` is row-major (index x + y*width), R32Float, tightly
    // packed for `width` x `height`. Level 0 == the field itself.
    struct ScalarFieldMipLevel
    {
        uint32_t           width  = 0;
        uint32_t           height = 0;
        std::vector<float> values;
    };

    // Build the full box-filter mip pyramid from a field's mip-0 samples. Level 0
    // is a copy of the input; each coarser level is the 2x2 box-filter of the one
    // above (mip k texel = average of the 2x2 mip k-1 texels it covers). The chain
    // length is floor(log2(max(width,height)))+1 -- the same count the resident
    // texture is created with (#209) and the clipmap VS clamps its sampled mip to.
    //
    // Non-power-of-two / odd / 1xN edges: a destination dimension is
    // max(src>>1, 1), so an odd source dimension leaves a single leftover
    // column/row whose 2x2 block is clamped to the texels that exist -- only the
    // covered texels are averaged, never out of bounds.
    std::vector<ScalarFieldMipLevel> build_scalar_field_mip_pyramid(
        const std::vector<float>& mip0,
        uint32_t width,
        uint32_t height);

}
