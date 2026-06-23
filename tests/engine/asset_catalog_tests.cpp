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

    // #197: scalar field residency now lives on the wozzits-rhi registry, so the
    // type is no longer withheld from the authoring catalog and shows up in the
    // editor asset browser. Peers still on the legacy residency tables (e.g.
    // vector field) stay withheld — that guards against the predicate being
    // gutted wholesale rather than narrowed to scalar field.
    TEST(AssetCatalog, ScalarFieldAuthorableAfterRhiMigration)
    {
        EXPECT_FALSE(
            ee::asset_type_needs_rhi_migration(ea::kAssetTypeScalarField));
        EXPECT_TRUE(
            ee::asset_type_needs_rhi_migration(ea::kAssetTypeVectorField));

        const std::vector<ee::AssetCatalogEntry> catalog =
            ee::build_asset_catalog();

        EXPECT_TRUE(catalog_contains(catalog, ea::kAssetTypeScalarField));
        EXPECT_FALSE(catalog_contains(catalog, ea::kAssetTypeVectorField));
    }
}
