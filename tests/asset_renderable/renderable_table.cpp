#include <gtest/gtest.h>

#include <engine/assets/renderable/renderable.h>
#include <engine/assets/type_extensions.h>

TEST(RenderableAssetTable, StoresAndRetrievesRenderable)
{
    wz::engine::assets::RenderableAssetTable table;

    wz::engine::assets::RenderableAssetData data;
    data.kind = wz::engine::assets::RenderableKind::Mesh;
    data.source_asset.content_hash = { 1, 0 };
    data.program = wz::engine::assets::BuiltinRenderProgram::MeshWireframeDebug;
    data.domain = wz::engine::assets::RenderDomain::Debug;
    data.policy_flags = wz::engine::assets::RenderPolicy_Wireframe;

    const auto handle = table.add(data);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.type, wz::engine::assets::kAssetTypeRenderable);

    const auto* stored = table.get(handle);

    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->valid());
    EXPECT_EQ(stored->kind, wz::engine::assets::RenderableKind::Mesh);
}

TEST(RenderableAssetTable, RejectsWrongHandleType)
{
    wz::engine::assets::RenderableAssetTable table;

    wz::engine::assets::RenderableAssetData data;
    data.kind = wz::engine::assets::RenderableKind::Mesh;
    data.source_asset.content_hash = { 1, 0 };

    const auto handle = table.add(data);
    ASSERT_TRUE(handle.valid());

    wz::asset::ResourceHandle wrong = handle;
    wrong.type = wz::asset::AssetType::Mesh;

    EXPECT_EQ(table.get(wrong), nullptr);
}

TEST(RenderableAssetTable, StoresScalarFieldRenderable)
{
    wz::engine::assets::RenderableAssetTable table;

    wz::engine::assets::RenderableAssetData data;
    data.kind = wz::engine::assets::RenderableKind::ScalarField;
    data.source_asset.content_hash = { 1, 0 };
    data.program = wz::engine::assets::BuiltinRenderProgram::ScalarFieldDebug;
    data.domain = wz::engine::assets::RenderDomain::Debug;
    data.policy_flags = wz::engine::assets::RenderPolicy_None;

    const auto handle = table.add(data);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.type, wz::engine::assets::kAssetTypeRenderable);

    const auto* stored = table.get(handle);

    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->valid());
    EXPECT_EQ(stored->kind, wz::engine::assets::RenderableKind::ScalarField);
    EXPECT_EQ(stored->program, wz::engine::assets::BuiltinRenderProgram::ScalarFieldDebug);
}