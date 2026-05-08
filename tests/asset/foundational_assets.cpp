// tests/asset/foundational_assets.cpp

#include <gtest/gtest.h>
#include <iterator>
#include <engine/assets/type_extensions.h>
#include <type_traits>

namespace
{
    using wz::asset::AssetType;
}

TEST(FoundationAssetTypes, StableNumericValues)
{
    using U = std::underlying_type_t<wz::asset::AssetType>;

    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeRawFile), static_cast<U>(64));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeTextFile), static_cast<U>(65));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeBinaryBlob), static_cast<U>(66));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeManifest), static_cast<U>(67));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeAssetBundle), static_cast<U>(68));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypePackage), static_cast<U>(69));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeImportedSourceFile), static_cast<U>(70));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeExternalReference), static_cast<U>(71));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeDirectory), static_cast<U>(72));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeArchive), static_cast<U>(73));


}