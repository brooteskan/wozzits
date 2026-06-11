// tests/asset/engine_disk_cache_provider_tests.cpp
//
// Dedicated EngineDiskCacheProvider coverage. The provider is otherwise
// exercised only indirectly through per-module disk-cache tests; these
// tests pin the can_load gating and the load() rejection of corrupt
// cache entries.

#include <gtest/gtest.h>

#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/engine_disk_cache_provider.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <cstdint>
#include <vector>

namespace
{
    using namespace wz::engine::assets;

    struct ProviderFixture
    {
        explicit ProviderFixture(EngineAssetCacheSettings settings)
            : cache_settings(std::move(settings))
            , provider(
                  cache_settings,
                  logger,
                  scalar_fields,
                  meshes,
                  mesh_derived_fields,
                  mesh_sparse_operators,
                  terrains,
                  terrain_visual_proxies,
                  collisions)
        {
        }

        wz::Logger logger;
        ScalarFieldTable scalar_fields{};
        MeshTable meshes{};
        MeshDerivedFieldTable mesh_derived_fields{};
        MeshSparseOperatorTable mesh_sparse_operators{};
        TerrainAssetTable terrains{};
        TerrainVisualProxyTable terrain_visual_proxies{};
        CollisionAssetTable collisions{};
        EngineAssetCacheSettings cache_settings;
        EngineDiskCacheProvider provider;
    };

    wz::fs::Path make_cache_root(const char* name)
    {
        const wz::fs::Path root =
            wz::fs::join(wz::fs::temp_directory_path(), name);
        wz::fs::create_directories(root);
        return root;
    }

    wz::asset::AssetKey make_key(uint64_t lo, uint64_t hi)
    {
        wz::asset::AssetKey key{};
        key.content_hash = { lo, hi };
        return key;
    }
}

TEST(EngineDiskCacheProvider, RejectsWhenCacheDisabled)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_disabled_test");
    settings.enabled = false;

    ProviderFixture fx(settings);

    EXPECT_FALSE(fx.provider.can_load(
        kScalarFieldProceduralSchema,
        kAssetTypeScalarField,
        make_key(1u, 2u)));
}

TEST(EngineDiskCacheProvider, RejectsWhenCacheRootEmpty)
{
    EngineAssetCacheSettings settings{};
    settings.enabled = true;

    ProviderFixture fx(settings);

    EXPECT_FALSE(fx.provider.can_load(
        kScalarFieldProceduralSchema,
        kAssetTypeScalarField,
        make_key(1u, 2u)));
}

TEST(EngineDiskCacheProvider, RejectsNonCacheableSchemaTypeCombination)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_combo_test");
    settings.enabled = true;

    ProviderFixture fx(settings);

    // GLB mesh schema paired with the wrong asset type must not match any
    // cacheable rule.
    EXPECT_FALSE(fx.provider.can_load(
        kGLBMeshSchema,
        kAssetTypeScalarField,
        make_key(3u, 4u)));
}

TEST(EngineDiskCacheProvider, RejectsCacheableKeyWithoutDiskEntry)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_missing_entry_test");
    settings.enabled = true;

    ProviderFixture fx(settings);

    EXPECT_FALSE(fx.provider.can_load(
        kScalarFieldProceduralSchema,
        kAssetTypeScalarField,
        make_key(5u, 6u)));
}

TEST(EngineDiskCacheProvider, AcceptsCacheableKeyWithDiskEntry)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_existing_entry_test");
    settings.enabled = true;

    const wz::asset::AssetKey key = make_key(7u, 8u);

    ASSERT_EQ(
        wz::fs::create_directories(internal::disk_cache_asset_directory(
            settings,
            internal::kScalarFieldDiskCacheKey.subdirectory)),
        wz::fs::FileError::None);
    const wz::fs::Path path = internal::disk_cache_asset_path(
        settings,
        internal::kScalarFieldDiskCacheKey.subdirectory,
        key,
        internal::kScalarFieldDiskCacheKey.seed_lo,
        internal::kScalarFieldDiskCacheKey.seed_hi);
    const std::vector<uint8_t> garbage{ 0x00u, 0x01u, 0x02u, 0x03u };
    ASSERT_EQ(wz::fs::write_file(path, garbage), wz::fs::FileError::None);

    ProviderFixture fx(settings);

    EXPECT_TRUE(fx.provider.can_load(
        kScalarFieldProceduralSchema,
        kAssetTypeScalarField,
        key));
}

TEST(EngineDiskCacheProvider, LoadRejectsCorruptDiskEntry)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_corrupt_entry_test");
    settings.enabled = true;

    const wz::asset::AssetKey key = make_key(9u, 10u);

    ASSERT_EQ(
        wz::fs::create_directories(internal::disk_cache_asset_directory(
            settings,
            internal::kScalarFieldDiskCacheKey.subdirectory)),
        wz::fs::FileError::None);
    const wz::fs::Path path = internal::disk_cache_asset_path(
        settings,
        internal::kScalarFieldDiskCacheKey.subdirectory,
        key,
        internal::kScalarFieldDiskCacheKey.seed_lo,
        internal::kScalarFieldDiskCacheKey.seed_hi);
    const std::vector<uint8_t> garbage(64u, 0xCDu);
    ASSERT_EQ(wz::fs::write_file(path, garbage), wz::fs::FileError::None);

    ProviderFixture fx(settings);

    // The entry exists, so can_load reports it; load must then reject the
    // corrupt payload instead of producing a handle.
    ASSERT_TRUE(fx.provider.can_load(
        kScalarFieldProceduralSchema,
        kAssetTypeScalarField,
        key));
    EXPECT_FALSE(fx.provider.load(
        kScalarFieldProceduralSchema,
        kAssetTypeScalarField,
        key).has_value());
}

TEST(EngineDiskCacheProvider, LoadRejectsUnknownSchema)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_unknown_schema_test");
    settings.enabled = true;

    ProviderFixture fx(settings);

    EXPECT_FALSE(fx.provider.load(
        kGLBMeshSchema,
        kAssetTypeScalarField,
        make_key(11u, 12u)).has_value());
}
