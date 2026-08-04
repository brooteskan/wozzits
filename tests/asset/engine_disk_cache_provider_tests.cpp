// tests/asset/engine_disk_cache_provider_tests.cpp
//
// Dedicated EngineDiskCacheProvider coverage. The provider is otherwise
// exercised only indirectly through per-module disk-cache tests; these
// tests pin the can_load gating and the load() rejection of corrupt
// cache entries.

#include <gtest/gtest.h>

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/disk_cache_checksum.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/engine_disk_cache_provider.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <cstdint>
#include <cstring>
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
    constexpr uint32_t kScalarFieldFormatVersion = 2u;

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
        append_disk_cache_checksum(out);
        return out;
    }

    void write_cache_entry(
        const EngineAssetCacheSettings& settings,
        const internal::DiskCacheKeySpec& spec,
        const wz::asset::AssetKey& key,
        const std::vector<uint8_t>& bytes)
    {
        ASSERT_EQ(
            wz::fs::create_directories(
                internal::disk_cache_asset_directory(
                    settings,
                    spec.subdirectory)),
            wz::fs::FileError::None);
        ASSERT_EQ(
            wz::fs::write_file(
                internal::disk_cache_asset_path(
                    settings,
                    spec.subdirectory,
                    key,
                    spec.seed_lo,
                    spec.seed_hi),
                bytes),
            wz::fs::FileError::None);
    }

    void write_scalar_field_entry(
        const EngineAssetCacheSettings& settings,
        const wz::asset::AssetKey& key,
        const std::vector<uint8_t>& bytes)
    {
        write_cache_entry(
            settings,
            internal::kScalarFieldDiskCacheKey,
            key,
            bytes);
    }

    // ── Sibling cache-entry corpora (B1-C4, the other formats) ────────────────
    //
    // bb2fe8fd range-checked the scalar-field descriptors; the identical
    // unchecked static_cast<Enum>(raw uint8) was live in six sibling loaders.
    // These are the same corpus shape for the ones whose format is flat enough
    // to mirror without duplicating a page of nested readers: each builds a
    // WELL-FORMED entry (its CONTROL test proves it loads), then puts one
    // descriptor out of range.
    //
    // NOT covered here, deliberately, and said out loud rather than left to be
    // inferred from the file: the collision and terrain-visual-proxy loaders
    // carry the same new guards without a byte-level pin. The proxy format is
    // deeply nested (chunks x lods x surfels x transition strips, each with its
    // own sub-reader) and collision's is ~45 fields; both mirrors would rot
    // faster than they would catch anything.
    //
    // As with the scalar-field corpus, the magic/version constants below are
    // duplicated from file-local constants in the compilers. If one is bumped,
    // that format's CONTROL test fails first and loudly -- re-cut the corpus,
    // do not paper over it.

    void put_asset_key(std::vector<uint8_t>& out, const wz::asset::AssetKey& k)
    {
        put(out, k.content_hash.lo);
        put(out, k.content_hash.hi);
        put(out, k.schema_hash.lo);
        put(out, k.schema_hash.hi);
        put(out, k.compiler_hash.lo);
        put(out, k.compiler_hash.hi);
        put(out, k.deps_hash.lo);
        put(out, k.deps_hash.hi);
    }

    // ── GLB mesh ──────────────────────────────────────────────────────────────
    constexpr uint32_t kMeshMagic = 0x4d435a57u;
    constexpr uint32_t kMeshFormatVersion = 2u;

    struct MeshEntryFields
    {
        uint8_t topology = static_cast<uint8_t>(
            MeshPrimitiveTopology::TriangleList);
        uint8_t index_format = static_cast<uint8_t>(MeshIndexFormat::UInt32);
    };

    // Mirrors serialize_mesh_asset.
    std::vector<uint8_t> make_mesh_entry(
        const wz::asset::AssetKey& key,
        const MeshEntryFields& f = {})
    {
        std::vector<uint8_t> out;
        put(out, kMeshMagic);
        put(out, kMeshFormatVersion);
        put(out, kMeshCompilerVersion);
        put_asset_key(out, key);
        put(out, f.topology);
        put(out, f.index_format);
        put(out, uint8_t{ 0 });                       // has_normals
        put(out, uint8_t{ 0 });                       // has_uv0

        const MeshVertex vertices[3]{};
        put(out, static_cast<uint64_t>(3));
        for (const MeshVertex& v : vertices) {
            put(out, v);
        }

        const uint32_t indices[3]{ 0u, 1u, 2u };
        put(out, static_cast<uint64_t>(3));
        for (const uint32_t i : indices) {
            put(out, i);
        }
        append_disk_cache_checksum(out);
        return out;
    }

    // ── Mesh terrain ──────────────────────────────────────────────────────────
    constexpr uint32_t kTerrainMagic = 0x54435a57u;
    constexpr uint32_t kTerrainFormatVersion = 2u;

    struct TerrainEntryFields
    {
        uint8_t representation = static_cast<uint8_t>(
            TerrainRepresentationKind::MeshSurface);
        uint8_t mesh_height_policy = static_cast<uint8_t>(
            TerrainMeshSurfaceHeightPolicy::HighestAcceptedSurface);
        uint8_t normal_source = static_cast<uint8_t>(
            TerrainNormalSource::DerivedGeometry);
        uint8_t uv_source = static_cast<uint8_t>(TerrainUVSource::None);
        uint8_t render_mode = static_cast<uint8_t>(TerrainRenderMode::None);
        uint8_t collision_mode = static_cast<uint8_t>(
            TerrainCollisionMode::None);
    };

    // Mirrors serialize_mesh_terrain_asset.
    std::vector<uint8_t> make_terrain_entry(
        const wz::asset::AssetKey& key,
        const TerrainEntryFields& f = {})
    {
        std::vector<uint8_t> out;
        put(out, kTerrainMagic);
        put(out, kTerrainFormatVersion);
        put(out, kTerrainCompilerVersion);
        put_asset_key(out, key);
        put(out, f.representation);
        put_asset_key(out, make_key(0xA1u, 0xA2u));   // source_asset (non-empty:
                                                      // TerrainAssetData::valid)
        put_asset_key(out, wz::asset::AssetKey{});    // height_field
        put_asset_key(out, wz::asset::AssetKey{});    // mesh
        put_asset_key(out, wz::asset::AssetKey{});    // normal_field
        put_asset_key(out, wz::asset::AssetKey{});    // material_mask_set
        put(out, f.mesh_height_policy);
        put(out, 0.0f);                               // min_surface_normal_y
        put(out, uint8_t{ 0 });                       // include_backfaces
        put(out, f.normal_source);
        put(out, f.uv_source);
        put(out, uint8_t{ 0 });                       // mesh_has_source_normals
        put(out, uint8_t{ 0 });                       // mesh_has_source_uv0
        put(out, uint32_t{ 0 });                      // mesh_triangle_count
        put(out, uint32_t{ 0 });   // mesh_accepted_surface_triangle_count
        put(out, uint32_t{ 0 });                      // mesh_visual_chunk_count
        put(out, 0.0f); put(out, 0.0f);               // origin[2]
        put(out, 1.0f); put(out, 1.0f);               // size[2]
        put(out, uint32_t{ 0 });                      // resolution_x
        put(out, uint32_t{ 0 });                      // resolution_y
        put(out, 1.0f);                               // vertical_scale
        put(out, 0.0f);                               // base_height
        put(out, 0.0f);                               // min_height
        put(out, 1.0f);                               // max_height
        put(out, 0.0f); put(out, 0.0f); put(out, 0.0f);  // bounds_min[3]
        put(out, 1.0f); put(out, 1.0f); put(out, 1.0f);  // bounds_max[3]
        put(out, f.render_mode);
        put(out, f.collision_mode);
        put(out, uint8_t{ 0 });                       // supports_height_query
        put(out, uint8_t{ 0 });                       // supports_ray_query
        put(out, uint8_t{ 0 });                       // supports_render_mesh

        put(out, static_cast<uint64_t>(0));           // height_samples
        put(out, static_cast<uint64_t>(0));           // mesh_surface_points
        put(out, static_cast<uint64_t>(0));           // mesh_surface_indices
        put(out, static_cast<uint64_t>(0));           // mesh_visual_indices
        put(out, static_cast<uint64_t>(0));           // mesh_visual_chunks
        append_disk_cache_checksum(out);
        return out;
    }

    // ── Mesh sparse operator ──────────────────────────────────────────────────
    constexpr uint32_t kSparseOperatorMagic = 0x4f535a57u;
    constexpr uint32_t kSparseOperatorFormatVersion = 2u;

    struct SparseOperatorEntryFields
    {
        uint8_t kind = static_cast<uint8_t>(
            MeshSparseOperatorKind::UniformAdjacency);
        uint8_t domain = static_cast<uint8_t>(MeshOperatorDomain::Vertex);
        uint8_t value_convention = static_cast<uint8_t>(
            MeshSparseOperatorValueConvention::NeighborWeights);
    };

    // Mirrors serialize_mesh_sparse_operator. One row, one nonzero, so
    // MeshSparseOperatorData::valid()'s CSR invariants hold.
    std::vector<uint8_t> make_sparse_operator_entry(
        const wz::asset::AssetKey& key,
        const SparseOperatorEntryFields& f = {})
    {
        std::vector<uint8_t> out;
        put(out, kSparseOperatorMagic);
        put(out, kSparseOperatorFormatVersion);
        put(out, kMeshSparseOperatorCompilerVersion);
        put_asset_key(out, key);
        put_asset_key(out, make_key(0xB1u, 0xB2u));   // source_mesh_key
        put(out, uint64_t{ 0xC1u });                  // source_topology_hash.lo
        put(out, uint64_t{ 0xC2u });                  // source_topology_hash.hi
        put(out, f.kind);
        put(out, f.domain);
        put(out, f.value_convention);
        put(out, uint32_t{ 1 });                      // row_count
        put(out, uint32_t{ 1 });                      // nonzero_count

        put(out, static_cast<uint64_t>(2));           // row_offsets
        put(out, uint32_t{ 0 });
        put(out, uint32_t{ 1 });
        put(out, static_cast<uint64_t>(1));           // col_indices
        put(out, uint32_t{ 0 });
        put(out, static_cast<uint64_t>(1));           // weights
        put(out, 1.0f);
        put(out, static_cast<uint64_t>(1));           // vertex_mass
        put(out, 1.0f);
        append_disk_cache_checksum(out);
        return out;
    }

    // ── Mesh derived field ────────────────────────────────────────────────────
    constexpr uint32_t kDerivedFieldMagic = 0x4d445a57u;
    constexpr uint32_t kDerivedFieldFormatVersion = 2u;

    struct DerivedFieldEntryFields
    {
        uint8_t domain = static_cast<uint8_t>(MeshDerivedFieldDomain::Vertex);
        uint8_t value_type = static_cast<uint8_t>(
            MeshDerivedFieldValueType::Float1);
    };

    // Mirrors serialize_mesh_derived_field_asset. One channel of one float per
    // element, so MeshDerivedFieldData::valid()'s offset/stride arithmetic
    // holds.
    std::vector<uint8_t> make_derived_field_entry(
        const wz::asset::AssetKey& key,
        const DerivedFieldEntryFields& f = {})
    {
        std::vector<uint8_t> out;
        put(out, kDerivedFieldMagic);
        put(out, kDerivedFieldFormatVersion);
        put(out, kMeshDerivedFieldCompilerVersion);
        put_asset_key(out, key);
        put_asset_key(out, make_key(0xD1u, 0xD2u));   // source_mesh_key
        put(out, uint64_t{ 0xE1u });                  // source_topology_hash.lo
        put(out, uint64_t{ 0xE2u });                  // source_topology_hash.hi
        put(out, f.domain);
        put(out, uint32_t{ 2 });                      // element_count

        put(out, static_cast<uint64_t>(1));           // channel count
        put(out, uint32_t{ 7 });                      // channel_id (non-zero)
        put(out, f.value_type);
        put(out, uint32_t{ 0 });                      // byte_offset
        put(out, uint32_t{ 8 });                      // byte_count = 2 * 4

        put(out, static_cast<uint64_t>(8));           // values
        put(out, 0.0f);
        put(out, 0.0f);
        append_disk_cache_checksum(out);
        return out;
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

// B1-C4 (the payload half). Magic, the versions, the stored key and the
// descriptor range checks all cover byte regions OTHER than the sample data,
// the min/max bounds, and the extents-as-written -- so a flip in any of those
// loaded silently, serving the wrong bytes under a key that still matched. The
// trailing blob checksum (#75 B1-C4) closes it. AcceptsWellFormedScalarFieldEntry
// is the control; these damage exactly the regions no other check inspects.
TEST(EngineDiskCacheProvider, ChecksumRejectsAFlippedPayloadByte)
{
    // Offsets measured from the end of the blob, for the 4-sample fixture:
    //   [ ... min(4) max(4) count(8) values(16) checksum(8) ]
    struct Case { const char* name; std::size_t from_end; };
    const Case cases[] = {
        { "last sample float", kDiskCacheChecksumSize + 1u },
        { "max_value",         kDiskCacheChecksumSize + 16u + 8u + 1u },
        { "min_value",         kDiskCacheChecksumSize + 16u + 8u + 4u + 1u },
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        EngineAssetCacheSettings settings{};
        settings.root = make_cache_root("wz_disk_cache_flip_payload_test");
        settings.enabled = true;

        const wz::asset::AssetKey key = make_key(91u, 92u);
        std::vector<uint8_t> blob = make_scalar_field_entry(key);
        ASSERT_GT(blob.size(), c.from_end);
        blob[blob.size() - c.from_end] ^= 0x01u;   // one bit, in a region no
                                                   // other check would notice
        write_scalar_field_entry(settings, key, blob);

        ProviderFixture fx(settings);
        ASSERT_TRUE(fx.provider.can_load(
            kScalarFieldProceduralSchema, kAssetTypeScalarField, key));
        EXPECT_FALSE(fx.provider.load(
            kScalarFieldProceduralSchema, kAssetTypeScalarField, key)
                .has_value());
    }
}

// B1-C4. The extents pass their own overflow/count check whenever the product
// still matches the sample count, so a reshape that preserves the total --
// 2x2x1 -> 4x1x1 -- changed what the field means while every extent check kept
// passing. Only a checksum over the header notices the edited dimension bytes.
TEST(EngineDiskCacheProvider, ChecksumRejectsADimsReshapePreservingTheTotal)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_reshape_test");
    settings.enabled = true;

    const wz::asset::AssetKey key = make_key(93u, 94u);
    std::vector<uint8_t> blob = make_scalar_field_entry(key);  // 2 x 2 x 1

    // width/height sit right after the header:
    //   magic(4) version(4) compiler_version(8) key(64) = 80, then width(4).
    constexpr std::size_t kWidthOffset = 4u + 4u + 8u + 64u;
    const uint32_t new_width = 4u;
    const uint32_t new_height = 1u;
    std::memcpy(blob.data() + kWidthOffset, &new_width, sizeof(new_width));
    std::memcpy(
        blob.data() + kWidthOffset + 4u, &new_height, sizeof(new_height));
    write_scalar_field_entry(settings, key, blob);

    ProviderFixture fx(settings);
    ASSERT_TRUE(fx.provider.can_load(
        kScalarFieldProceduralSchema, kAssetTypeScalarField, key));
    EXPECT_FALSE(fx.provider.load(
        kScalarFieldProceduralSchema, kAssetTypeScalarField, key).has_value());
}

// An entry missing its trailing checksum -- a torn write, or an entry written
// by the pre-checksum format -- is a miss, not a read of whatever remains. This
// is also how the version bump's old entries are refused: they carry no
// checksum, so verification fails before the version word is even read.
TEST(EngineDiskCacheProvider, ChecksumRejectsAnEntryWithoutATrailingChecksum)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_no_checksum_test");
    settings.enabled = true;

    const wz::asset::AssetKey key = make_key(95u, 96u);
    std::vector<uint8_t> blob = make_scalar_field_entry(key);
    ASSERT_GT(blob.size(), kDiskCacheChecksumSize);
    blob.resize(blob.size() - kDiskCacheChecksumSize);   // drop the checksum
    write_scalar_field_entry(settings, key, blob);

    ProviderFixture fx(settings);
    ASSERT_TRUE(fx.provider.can_load(
        kScalarFieldProceduralSchema, kAssetTypeScalarField, key));
    EXPECT_FALSE(fx.provider.load(
        kScalarFieldProceduralSchema, kAssetTypeScalarField, key).has_value());
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

// ─── Sibling cache descriptor ranges (B1-C4, the other formats) ───────────────
//
// The same defect bb2fe8fd closed for the scalar field: a descriptor stored as
// a raw uint8 and static_cast straight back, so a flipped byte produced an
// out-of-range enum that no other check covered. Each format gets a CONTROL
// first -- a rejection is only evidence once the well-formed entry is known to
// load.
//
// Which of these were genuinely unguarded, MEASURED by neutering each new
// check in turn and seeing which cases below stop failing -- not assumed, and
// the measurement corrected two guesses:
//
//   mesh topology / index_format     unguarded (MeshData::valid inspects only
//                                    the vertex and index arrays)
//   terrain height policy, normal
//     source, uv source, render
//     mode, collision mode           unguarded (TerrainAssetData::valid
//                                    inspects none of them)
//   terrain representation           ALREADY rejected on THIS path:
//                                    load_cached_mesh_terrain additionally
//                                    requires representation == MeshSurface,
//                                    so an unknown ordinal never got through
//                                    here. (TerrainAssetData::valid alone
//                                    would have let it: it branches on
//                                    HeightField and returns true for
//                                    everything else.)
//   sparse operator kind, domain,
//     value_convention               unguarded (valid checks CSR shape only)
//   derived field domain             unguarded
//   derived field value_type         ALREADY rejected, via
//                                    mesh_derived_field_value_stride returning
//                                    0 for an unknown ordinal and valid()
//                                    refusing stride 0.
//
// The two "already rejected" rows keep their cases here anyway: they cost
// nothing, they pin behaviour that is currently incidental to another check,
// and they are the ones that would silently stop being covered if that other
// check ever moved.

TEST(EngineDiskCacheProvider, AcceptsWellFormedMeshEntry)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_mesh_control_test");
    settings.enabled = true;

    const wz::asset::AssetKey key = make_key(51u, 52u);
    write_cache_entry(
        settings, internal::kGLBMeshDiskCacheKey, key, make_mesh_entry(key));

    ProviderFixture fx(settings);
    ASSERT_TRUE(fx.provider.can_load(kGLBMeshSchema, kAssetTypeMesh, key));
    const auto handle = fx.provider.load(kGLBMeshSchema, kAssetTypeMesh, key);
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(handle->valid());
}

TEST(EngineDiskCacheProvider, MeshLoadRejectsOutOfRangeDescriptorOrdinals)
{
    struct Case
    {
        const char* name;
        void (*corrupt)(MeshEntryFields&);
    };
    const Case cases[] = {
        { "topology", [](MeshEntryFields& f) {
              f.topology = static_cast<uint8_t>(
                  MeshPrimitiveTopology::TriangleList) + 1; } },
        { "index_format", [](MeshEntryFields& f) {
              f.index_format = static_cast<uint8_t>(
                  MeshIndexFormat::UInt32) + 1; } },
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        EngineAssetCacheSettings settings{};
        settings.root = make_cache_root("wz_disk_cache_mesh_ordinal_test");
        settings.enabled = true;

        const wz::asset::AssetKey key = make_key(53u, 54u);
        MeshEntryFields fields{};
        c.corrupt(fields);
        write_cache_entry(
            settings,
            internal::kGLBMeshDiskCacheKey,
            key,
            make_mesh_entry(key, fields));

        ProviderFixture fx(settings);
        ASSERT_TRUE(fx.provider.can_load(kGLBMeshSchema, kAssetTypeMesh, key));
        EXPECT_FALSE(
            fx.provider.load(kGLBMeshSchema, kAssetTypeMesh, key).has_value());
    }
}

TEST(EngineDiskCacheProvider, AcceptsWellFormedTerrainEntry)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_terrain_control_test");
    settings.enabled = true;

    const wz::asset::AssetKey key = make_key(61u, 62u);
    write_cache_entry(
        settings,
        internal::kMeshTerrainDiskCacheKey,
        key,
        make_terrain_entry(key));

    ProviderFixture fx(settings);
    ASSERT_TRUE(
        fx.provider.can_load(kTerrainFromMeshSchema, kAssetTypeTerrain, key));
    const auto handle =
        fx.provider.load(kTerrainFromMeshSchema, kAssetTypeTerrain, key);
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(handle->valid());
}

TEST(EngineDiskCacheProvider, TerrainLoadRejectsOutOfRangeDescriptorOrdinals)
{
    struct Case
    {
        const char* name;
        void (*corrupt)(TerrainEntryFields&);
    };
    const Case cases[] = {
        { "representation", [](TerrainEntryFields& f) {
              f.representation = static_cast<uint8_t>(
                  TerrainRepresentationKind::MeshSurface) + 1; } },
        { "mesh_height_policy", [](TerrainEntryFields& f) {
              f.mesh_height_policy = static_cast<uint8_t>(
                  TerrainMeshSurfaceHeightPolicy::HighestAcceptedSurface)
                  + 1; } },
        { "normal_source", [](TerrainEntryFields& f) {
              f.normal_source = static_cast<uint8_t>(
                  TerrainNormalSource::ImportedField) + 1; } },
        { "uv_source", [](TerrainEntryFields& f) {
              f.uv_source = static_cast<uint8_t>(
                  TerrainUVSource::ImportedField) + 1; } },
        { "render_mode", [](TerrainEntryFields& f) {
              f.render_mode = static_cast<uint8_t>(
                  TerrainRenderMode::DebugMesh) + 1; } },
        { "collision_mode", [](TerrainEntryFields& f) {
              f.collision_mode = static_cast<uint8_t>(
                  TerrainCollisionMode::MeshSurface) + 1; } },
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        EngineAssetCacheSettings settings{};
        settings.root = make_cache_root("wz_disk_cache_terrain_ordinal_test");
        settings.enabled = true;

        const wz::asset::AssetKey key = make_key(63u, 64u);
        TerrainEntryFields fields{};
        c.corrupt(fields);
        write_cache_entry(
            settings,
            internal::kMeshTerrainDiskCacheKey,
            key,
            make_terrain_entry(key, fields));

        ProviderFixture fx(settings);
        ASSERT_TRUE(fx.provider.can_load(
            kTerrainFromMeshSchema, kAssetTypeTerrain, key));
        EXPECT_FALSE(
            fx.provider.load(kTerrainFromMeshSchema, kAssetTypeTerrain, key)
                .has_value());
    }
}

TEST(EngineDiskCacheProvider, AcceptsWellFormedSparseOperatorEntry)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_sparse_op_control_test");
    settings.enabled = true;

    const wz::asset::AssetKey key = make_key(71u, 72u);
    write_cache_entry(
        settings,
        internal::kMeshSparseOperatorDiskCacheKey,
        key,
        make_sparse_operator_entry(key));

    ProviderFixture fx(settings);
    ASSERT_TRUE(fx.provider.can_load(
        kMeshSparseOperatorSchema, kAssetTypeMeshSparseOperator, key));
    const auto handle = fx.provider.load(
        kMeshSparseOperatorSchema, kAssetTypeMeshSparseOperator, key);
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(handle->valid());
}

TEST(EngineDiskCacheProvider,
     SparseOperatorLoadRejectsOutOfRangeDescriptorOrdinals)
{
    struct Case
    {
        const char* name;
        void (*corrupt)(SparseOperatorEntryFields&);
    };
    const Case cases[] = {
        { "kind", [](SparseOperatorEntryFields& f) {
              f.kind = static_cast<uint8_t>(
                  MeshSparseOperatorKind::UniformAdjacency) + 1; } },
        { "domain", [](SparseOperatorEntryFields& f) {
              f.domain = static_cast<uint8_t>(
                  MeshDerivedFieldDomain::Corner) + 1; } },
        { "value_convention", [](SparseOperatorEntryFields& f) {
              f.value_convention = static_cast<uint8_t>(
                  MeshSparseOperatorValueConvention::FullMatrixEntries)
                  + 1; } },
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        EngineAssetCacheSettings settings{};
        settings.root = make_cache_root("wz_disk_cache_sparse_op_ordinal_test");
        settings.enabled = true;

        const wz::asset::AssetKey key = make_key(73u, 74u);
        SparseOperatorEntryFields fields{};
        c.corrupt(fields);
        write_cache_entry(
            settings,
            internal::kMeshSparseOperatorDiskCacheKey,
            key,
            make_sparse_operator_entry(key, fields));

        ProviderFixture fx(settings);
        ASSERT_TRUE(fx.provider.can_load(
            kMeshSparseOperatorSchema, kAssetTypeMeshSparseOperator, key));
        EXPECT_FALSE(
            fx.provider.load(
                kMeshSparseOperatorSchema, kAssetTypeMeshSparseOperator, key)
                .has_value());
    }
}

TEST(EngineDiskCacheProvider, AcceptsWellFormedDerivedFieldEntry)
{
    EngineAssetCacheSettings settings{};
    settings.root = make_cache_root("wz_disk_cache_derived_control_test");
    settings.enabled = true;

    const wz::asset::AssetKey key = make_key(81u, 82u);
    write_cache_entry(
        settings,
        internal::kMeshDerivedFieldDiskCacheKey,
        key,
        make_derived_field_entry(key));

    ProviderFixture fx(settings);
    ASSERT_TRUE(fx.provider.can_load(
        kMeshDerivedFieldExplicitSchema, kAssetTypeMeshDerivedField, key));
    const auto handle = fx.provider.load(
        kMeshDerivedFieldExplicitSchema, kAssetTypeMeshDerivedField, key);
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(handle->valid());
}

TEST(EngineDiskCacheProvider,
     DerivedFieldLoadRejectsOutOfRangeDescriptorOrdinals)
{
    struct Case
    {
        const char* name;
        void (*corrupt)(DerivedFieldEntryFields&);
    };
    const Case cases[] = {
        { "domain", [](DerivedFieldEntryFields& f) {
              f.domain = static_cast<uint8_t>(
                  MeshDerivedFieldDomain::Corner) + 1; } },
        { "value_type", [](DerivedFieldEntryFields& f) {
              f.value_type = static_cast<uint8_t>(
                  MeshDerivedFieldValueType::UInt1) + 1; } },
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        EngineAssetCacheSettings settings{};
        settings.root = make_cache_root("wz_disk_cache_derived_ordinal_test");
        settings.enabled = true;

        const wz::asset::AssetKey key = make_key(83u, 84u);
        DerivedFieldEntryFields fields{};
        c.corrupt(fields);
        write_cache_entry(
            settings,
            internal::kMeshDerivedFieldDiskCacheKey,
            key,
            make_derived_field_entry(key, fields));

        ProviderFixture fx(settings);
        ASSERT_TRUE(fx.provider.can_load(
            kMeshDerivedFieldExplicitSchema, kAssetTypeMeshDerivedField, key));
        EXPECT_FALSE(
            fx.provider.load(
                kMeshDerivedFieldExplicitSchema,
                kAssetTypeMeshDerivedField,
                key).has_value());
    }
}
