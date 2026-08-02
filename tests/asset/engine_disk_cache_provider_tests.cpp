// tests/asset/engine_disk_cache_provider_tests.cpp
//
// Dedicated EngineDiskCacheProvider coverage. The provider is otherwise
// exercised only indirectly through per-module disk-cache tests; these
// tests pin the can_load gating and the load() rejection of corrupt
// cache entries.

#include <gtest/gtest.h>

#include <engine/assets/compiler_version_tokens.h>
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

    // ── Scalar-field cache-entry corpus (B1-T4) ───────────────────────────────
    //
    // The pre-existing corrupt-entry test writes all-0xCD, which dies at the
    // magic word and therefore proves nothing about the checks downstream of
    // it. These build a WELL-FORMED entry first -- a rejection is only evidence
    // once the control is known to load -- and then flip one field at a time.
    //
    // The three header constants are duplicated from
    // scalar_field_compilers.cpp, where they are file-local. If either is
    // bumped, AcceptsWellFormedScalarFieldEntry below fails first and loudly:
    // that is the signal to re-cut this corpus, not to paper over it.
    constexpr uint32_t kScalarFieldMagic = 0x53465a57u;
    constexpr uint32_t kScalarFieldFormatVersion = 1u;

    template<typename T>
    void put(std::vector<uint8_t>& out, const T& value)
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        out.insert(out.end(), bytes, bytes + sizeof(T));
    }

    struct ScalarFieldEntryFields
    {
        uint32_t width = 2;
        uint32_t height = 2;
        uint32_t depth = 1;
        uint8_t format = static_cast<uint8_t>(ScalarFieldFormat::Float32);
        uint8_t domain_kind =
            static_cast<uint8_t>(ScalarFieldDomainKind::Spatial2D);
        uint8_t layout =
            static_cast<uint8_t>(ScalarFieldSampleLayout::TexelCentered);
        uint8_t origin = static_cast<uint8_t>(ScalarFieldOrigin::TopLeft);
        std::vector<float> values{ 1.0f, 2.0f, 3.0f, 4.0f };
    };

    // Mirrors serialize_scalar_field_asset's layout.
    std::vector<uint8_t> make_scalar_field_entry(
        const wz::asset::AssetKey& key,
        const ScalarFieldEntryFields& f = {})
    {
        std::vector<uint8_t> out;
        put(out, kScalarFieldMagic);
        put(out, kScalarFieldFormatVersion);
        put(out, kScalarFieldCompilerVersion);
        put(out, key.content_hash.lo);
        put(out, key.content_hash.hi);
        put(out, key.schema_hash.lo);
        put(out, key.schema_hash.hi);
        put(out, key.compiler_hash.lo);
        put(out, key.compiler_hash.hi);
        put(out, key.deps_hash.lo);
        put(out, key.deps_hash.hi);
        put(out, f.width);
        put(out, f.height);
        put(out, f.depth);
        put(out, f.format);
        put(out, f.domain_kind);
        put(out, f.layout);
        put(out, f.origin);
        put(out, 1.0f);                                  // min_value
        put(out, 4.0f);                                  // max_value
        put(out, static_cast<uint64_t>(f.values.size()));
        for (const float v : f.values) {
            put(out, v);
        }
        return out;
    }

    void write_scalar_field_entry(
        const EngineAssetCacheSettings& settings,
        const wz::asset::AssetKey& key,
        const std::vector<uint8_t>& bytes)
    {
        ASSERT_EQ(
            wz::fs::create_directories(internal::disk_cache_asset_directory(
                settings,
                internal::kScalarFieldDiskCacheKey.subdirectory)),
            wz::fs::FileError::None);
        ASSERT_EQ(
            wz::fs::write_file(
                internal::disk_cache_asset_path(
                    settings,
                    internal::kScalarFieldDiskCacheKey.subdirectory,
                    key,
                    internal::kScalarFieldDiskCacheKey.seed_lo,
                    internal::kScalarFieldDiskCacheKey.seed_hi),
                bytes),
            wz::fs::FileError::None);
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

// CONTROL for the two tests below: without it, a rejection is indistinguishable
// from a broken entry builder.
TEST(EngineDiskCacheProvider, AcceptsWellFormedScalarFieldEntry)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_wellformed_test");
    settings.enabled = true;

    const wz::asset::AssetKey key = make_key(21u, 22u);
    write_scalar_field_entry(settings, key, make_scalar_field_entry(key));

    ProviderFixture fx(settings);
    ASSERT_TRUE(fx.provider.can_load(
        kScalarFieldProceduralSchema, kAssetTypeScalarField, key));

    const auto handle = fx.provider.load(
        kScalarFieldProceduralSchema, kAssetTypeScalarField, key);
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(handle->valid());
}

// B1-C4. The four descriptors are stored as raw uint8 and were static_cast
// straight back with no range check, so a flipped byte produced an out-of-range
// enum that every other check in the loader missed: magic, format version,
// compiler version and the stored key all cover different byte regions, and
// valid() only inspects the extents and the sample count.
TEST(EngineDiskCacheProvider, LoadRejectsOutOfRangeDescriptorOrdinals)
{
    struct Case
    {
        const char* name;
        void (*corrupt)(ScalarFieldEntryFields&);
    };
    const Case cases[] = {
        { "format",      [](ScalarFieldEntryFields& f) { f.format = 1; } },
        { "domain_kind", [](ScalarFieldEntryFields& f) {
              f.domain_kind = static_cast<uint8_t>(
                  ScalarFieldDomainKind::BakedComputation) + 1; } },
        { "layout",      [](ScalarFieldEntryFields& f) { f.layout = 1; } },
        { "origin",      [](ScalarFieldEntryFields& f) { f.origin = 1; } },
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        EngineAssetCacheSettings settings{};
        settings.root = make_cache_root("wz_disk_cache_bad_ordinal_test");
        settings.enabled = true;

        const wz::asset::AssetKey key = make_key(31u, 32u);
        ScalarFieldEntryFields fields{};
        c.corrupt(fields);
        write_scalar_field_entry(
            settings, key, make_scalar_field_entry(key, fields));

        ProviderFixture fx(settings);
        ASSERT_TRUE(fx.provider.can_load(
            kScalarFieldProceduralSchema, kAssetTypeScalarField, key));
        EXPECT_FALSE(fx.provider.load(
            kScalarFieldProceduralSchema, kAssetTypeScalarField, key)
                .has_value());
    }
}

// B1-C3. Extents whose product overflows uint32 wrapped to zero, so an entry
// declaring billions of samples while carrying none satisfied valid().
TEST(EngineDiskCacheProvider, LoadRejectsExtentsWhoseProductOverflows)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_overflow_extent_test");
    settings.enabled = true;

    const wz::asset::AssetKey key = make_key(41u, 42u);
    ScalarFieldEntryFields fields{};
    fields.width = 65536;
    fields.height = 65536;
    fields.depth = 1;
    fields.values.clear();
    write_scalar_field_entry(
        settings, key, make_scalar_field_entry(key, fields));

    ProviderFixture fx(settings);
    ASSERT_TRUE(fx.provider.can_load(
        kScalarFieldProceduralSchema, kAssetTypeScalarField, key));
    EXPECT_FALSE(fx.provider.load(
        kScalarFieldProceduralSchema, kAssetTypeScalarField, key).has_value());
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
