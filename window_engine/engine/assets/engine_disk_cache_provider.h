#pragma once

#include <asset/external_cache_provider.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/collision/collision.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/scalar_field/scalar_field_compilers.h>  // RhiResourceTracker, residency
#include <engine/assets/terrain/terrain.h>
#include <engine/assets/terrain/terrain_visual_proxy.h>
#include <logging/logger.h>

namespace wz::rhi { class GpuResourceRegistry; }

#include <optional>

namespace wz::engine::assets
{
    class EngineDiskCacheProvider final : public wz::asset::ExternalCacheProvider
    {
    public:
        EngineDiskCacheProvider(
            const EngineAssetCacheSettings& cache_settings,
            wz::Logger& logger,
            ScalarFieldTable& scalar_fields,
            MeshTable& meshes,
            MeshDerivedFieldTable& mesh_derived_fields,
            MeshSparseOperatorTable& mesh_sparse_operators,
            TerrainAssetTable& terrains,
            TerrainVisualProxyTable& terrain_visual_proxies,
            CollisionAssetTable& collisions,
            // GPU-residency hook: a cache-served asset must re-publish the same
            // rhi residency a fresh compile would (issue #334). Null gpu_resources
            // (device-only library) skips it, matching the compilers.
            wz::rhi::GpuResourceRegistry* gpu_resources = nullptr,
            internal::RhiResourceTracker rhi_resource_tracker = {});

        bool can_load(
            wz::asset::SchemaID schema,
            wz::asset::AssetType type,
            const wz::asset::AssetKey& key) const override;

        std::optional<wz::asset::ResourceHandle> load(
            wz::asset::SchemaID schema,
            wz::asset::AssetType type,
            const wz::asset::AssetKey& key) override;

        // True for exactly the (schema, type) pairs this provider serves, i.e. the
        // types the compilers store_cached (kept in lockstep via disk_cache_spec).
        // Key-independent, so a sealed resolve can flag a cacheable-but-absent
        // asset without an on-disk probe.
        bool is_cacheable(
            wz::asset::SchemaID schema,
            wz::asset::AssetType type) const override;

        bool sealed() const noexcept override { return cache_settings_.sealed; }

    private:
        const EngineAssetCacheSettings& cache_settings_;
        wz::Logger& logger_;
        ScalarFieldTable& scalar_fields_;
        MeshTable& meshes_;
        MeshDerivedFieldTable& mesh_derived_fields_;
        MeshSparseOperatorTable& mesh_sparse_operators_;
        TerrainAssetTable& terrains_;
        TerrainVisualProxyTable& terrain_visual_proxies_;
        CollisionAssetTable& collisions_;
        wz::rhi::GpuResourceRegistry* gpu_resources_ = nullptr;
        internal::RhiResourceTracker rhi_resource_tracker_;
    };

    // Free predicate: whether the engine disk cache serves (schema, type) when the
    // entry is present — the type whitelist only, independent of cache settings and
    // of any key's on-disk existence. Single source of truth (disk_cache_spec)
    // shared by EngineDiskCacheProvider::is_cacheable AND the bundle closure walker
    // (issue #334), so "what the sealed cache serves" cannot fork between the
    // runtime that reads the cache and the exporter that decides which sources a
    // sealed bundle may strip.
    [[nodiscard]] bool is_disk_cacheable(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type);
}
