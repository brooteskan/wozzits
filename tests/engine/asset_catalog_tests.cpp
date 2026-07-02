#include <gtest/gtest.h>

#include <engine/editor/asset_catalog.h>

#include <engine/assets/type_extensions.h>
#include <asset/types.h>

#include <vector>

namespace
{
    namespace ee = wz::engine::editor;
    namespace ea = wz::engine::assets;

    bool catalog_contains(
        const std::vector<ee::AssetCatalogEntry>& catalog,
        wz::asset::AssetType type)
    {
        for (const ee::AssetCatalogEntry& entry : catalog) {
            if (entry.type == type) {
                return true;
            }
        }
        return false;
    }

    // #197 / #201: scalar-field, vector-field, and environment-map residency now
    // live on the wozzits-rhi registry, so those types are no longer withheld
    // from the authoring catalog and show up in the editor asset browser. Peers
    // still on the legacy residency tables (e.g. GPU sparse mesh) stay withheld —
    // that guards against the predicate being gutted wholesale rather than
    // narrowed to the migrated types.
    TEST(AssetCatalog, MigratedResidencyTypesAreAuthorable)
    {
        EXPECT_FALSE(
            ee::asset_type_needs_rhi_migration(ea::kAssetTypeScalarField));
        EXPECT_FALSE(
            ee::asset_type_needs_rhi_migration(ea::kAssetTypeVectorField));
        EXPECT_FALSE(
            ee::asset_type_needs_rhi_migration(ea::kAssetTypeEnvironmentMap));
        EXPECT_TRUE(
            ee::asset_type_needs_rhi_migration(ea::kAssetTypeGpuSparseMesh));

        const std::vector<ee::AssetCatalogEntry> catalog =
            ee::build_asset_catalog();

        EXPECT_TRUE(catalog_contains(catalog, ea::kAssetTypeScalarField));
        EXPECT_TRUE(catalog_contains(catalog, ea::kAssetTypeVectorField));
        EXPECT_TRUE(catalog_contains(catalog, ea::kAssetTypeEnvironmentMap));
        EXPECT_FALSE(catalog_contains(catalog, ea::kAssetTypeGpuSparseMesh));
    }
}
