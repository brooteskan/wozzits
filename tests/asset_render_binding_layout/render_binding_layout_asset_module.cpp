#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/render_binding_layout.h>
#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <string>

namespace
{
    using namespace wz::engine::assets;

    // The preset-2 (clipmap landscape) SRG expressed as a layout: the shape
    // the issue #227 seam test reproduces through a custom render program.
    RenderBindingLayoutData clipmap_layout()
    {
        RenderBindingLayoutData layout{};
        layout.constants_semantic = "clipmap";
        layout.constants_visibility = ShaderVisibility::All;
        layout.constants_head = RenderBindingConstantsHead::Clipmap32;
        layout.bindings = {
            {
                .semantic = "pulled_mesh_positions",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::Vertex,
            },
            {
                .semantic = "pulled_mesh_indices",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::Vertex,
            },
            {
                .semantic = "scalar_field_texture",
                .kind = RenderBindingKind::TextureSrv,
                .visibility = ShaderVisibility::Vertex,
            },
        };
        layout.samplers = {
            {
                .kind = StaticSamplerKind::LinearClamp,
                .visibility = ShaderVisibility::Vertex,
            },
        };
        return layout;
    }

}

TEST(RenderBindingLayoutAssetModule, CreateRejectsEmptyName)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(), "wozzits_rbl_reject_name_tests");
    wz::fs::create_directories(root);

    EngineAssetLibrary assets(device, logger, root);

    const auto asset =
        assets.render_binding_layouts().create_render_binding_layout({
            .name = "",
            .layout = clipmap_layout(),
        });

    EXPECT_FALSE(asset.valid());
}

TEST(RenderBindingLayoutAssetModule, CreateRejectsInvalidLayout)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(), "wozzits_rbl_reject_layout_tests");
    wz::fs::create_directories(root);

    EngineAssetLibrary assets(device, logger, root);

    RenderBindingLayoutData layout = clipmap_layout();
    layout.bindings.push_back(layout.bindings[0]);  // duplicate semantic

    const auto asset =
        assets.render_binding_layouts().create_render_binding_layout({
            .name = "layout/dup_semantic",
            .layout = layout,
        });

    EXPECT_FALSE(asset.valid());
}

TEST(RenderBindingLayoutAssetModule, CompileRoundTrip)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(), "wozzits_rbl_roundtrip_tests");
    wz::fs::create_directories(root);

    EngineAssetLibrary assets(device, logger, root);

    RenderBindingLayoutData layout = clipmap_layout();
    layout.constant_fields = {
        { .name = "tint", .type = RenderBindingConstantType::Color },
        { .name = "strength", .type = RenderBindingConstantType::Float },
    };
    layout.constants_dwords = 40;  // 32 head + 5 tail + padding

    const auto asset =
        assets.render_binding_layouts().create_render_binding_layout({
            .name = "layout/clipmap_roundtrip",
            .layout = layout,
        });
    ASSERT_TRUE(asset.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle =
        assets.render_binding_layouts().get_render_binding_layout(asset);
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeRenderBindingLayout);

    const RenderBindingLayoutData* data =
        assets.render_binding_layouts().get_render_binding_layout_data(
            handle);
    ASSERT_NE(data, nullptr);

    EXPECT_EQ(data->constants_semantic, "clipmap");
    EXPECT_EQ(data->constants_visibility, ShaderVisibility::All);
    EXPECT_EQ(data->constants_head, RenderBindingConstantsHead::Clipmap32);
    EXPECT_EQ(data->constants_dwords, 40u);
    EXPECT_EQ(data->total_constants_dwords(), 40u);
    ASSERT_EQ(data->constant_fields.size(), 2u);
    EXPECT_EQ(data->constant_fields[0].name, "tint");
    EXPECT_EQ(data->constant_fields[0].type, RenderBindingConstantType::Color);
    EXPECT_EQ(data->constant_fields[1].name, "strength");
    EXPECT_EQ(data->constant_fields[1].type, RenderBindingConstantType::Float);
    ASSERT_EQ(data->bindings.size(), 3u);
    EXPECT_EQ(data->bindings[0].semantic, "pulled_mesh_positions");
    EXPECT_EQ(data->bindings[2].kind, RenderBindingKind::TextureSrv);
    ASSERT_EQ(data->samplers.size(), 1u);
    EXPECT_EQ(data->samplers[0].kind, StaticSamplerKind::LinearClamp);
    EXPECT_EQ(data->samplers[0].visibility, ShaderVisibility::Vertex);
}

TEST(RenderBindingLayout, SrgDerivesRegistersFromRowOrder)
{
    RenderBindingLayoutSrg srg{};
    std::string error;
    ASSERT_TRUE(build_render_binding_layout_srg(clipmap_layout(), srg, error))
        << error;

    // One root-constant block: b0 space2, 32 dwords derived from the head.
    ASSERT_EQ(srg.root_constants.size(), 1u);
    EXPECT_EQ(srg.root_constants[0].visibility, ShaderVisibility::All);
    EXPECT_EQ(srg.root_constants[0].shader_register, 0u);
    EXPECT_EQ(srg.root_constants[0].register_space, 2u);
    EXPECT_EQ(srg.root_constants[0].value_count, 32u);
    EXPECT_EQ(srg.root_constants[0].semantic, "clipmap");

    // t0/t1/t2 space2 in row order.
    ASSERT_EQ(srg.descriptor_bindings.size(), 3u);
    for (uint32_t i = 0; i < 3u; ++i) {
        EXPECT_EQ(srg.descriptor_bindings[i].shader_register, i);
        EXPECT_EQ(srg.descriptor_bindings[i].register_space, 2u);
        EXPECT_EQ(srg.descriptor_bindings[i].descriptor_count, 1u);
    }
    EXPECT_EQ(
        srg.descriptor_bindings[0].kind,
        DescriptorKind::StructuredBufferSRV);
    EXPECT_EQ(
        srg.descriptor_bindings[0].semantic,
        DescriptorSemantic::PulledMeshPositions);
    EXPECT_EQ(
        srg.descriptor_bindings[1].semantic,
        DescriptorSemantic::PulledMeshIndices);
    EXPECT_EQ(srg.descriptor_bindings[2].kind, DescriptorKind::TextureSRV);
    EXPECT_EQ(
        srg.descriptor_bindings[2].semantic,
        DescriptorSemantic::ScalarFieldTexture);

    // s0 space2.
    ASSERT_EQ(srg.static_samplers.size(), 1u);
    EXPECT_EQ(srg.static_samplers[0].kind, StaticSamplerKind::LinearClamp);
    EXPECT_EQ(srg.static_samplers[0].visibility, ShaderVisibility::Vertex);
    EXPECT_EQ(srg.static_samplers[0].shader_register, 0u);
    EXPECT_EQ(srg.static_samplers[0].register_space, 2u);
}

TEST(RenderBindingLayout, SrgRejectsUnknownSemantic)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.bindings[1].semantic = "no_such_semantic";

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
    EXPECT_NE(error.find("no_such_semantic"), std::string::npos);
}

TEST(RenderBindingLayout, SrgRejectsUnknownIsNotAuthorable)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.bindings[1].semantic = "unknown";

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
}

TEST(RenderBindingLayout, SrgRejectsUndersizedConstantBlock)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.constants_dwords = 16;  // < 32-dword head

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
}

TEST(RenderBindingLayout, SrgRejectsConstantsWithoutSemantic)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.constants_semantic.clear();

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
}

TEST(RenderBindingLayout, SrgRejectsDuplicateFieldNames)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.constant_fields = {
        { .name = "tint", .type = RenderBindingConstantType::Color },
        { .name = "tint", .type = RenderBindingConstantType::Float },
    };

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
}

TEST(RenderBindingLayout, DerivedDwordsAreHeadPlusTail)
{
    RenderBindingLayoutData layout{};
    layout.constants_semantic = "mesh_style";
    layout.constants_head = RenderBindingConstantsHead::Mvp16;
    layout.constant_fields = {
        { .name = "wireframe_color", .type = RenderBindingConstantType::Color },
        { .name = "surface_color", .type = RenderBindingConstantType::Color },
        { .name = "params", .type = RenderBindingConstantType::Float4 },
    };

    // The preset-4 mesh_style block: mvp16 head + 12 dwords of style tail.
    EXPECT_EQ(layout.tail_dwords(), 12u);
    EXPECT_EQ(layout.total_constants_dwords(), 28u);
    EXPECT_TRUE(layout.valid());
}

TEST(RenderBindingLayout, KeyChangesWithEachSection)
{
    const RenderBindingLayoutData base = clipmap_layout();
    const wz::asset::AssetKey base_key =
        make_render_binding_layout_key("layout/key", base);

    {
        RenderBindingLayoutData changed = base;
        changed.constants_semantic = "other";
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/key", changed));
    }
    {
        RenderBindingLayoutData changed = base;
        changed.constants_visibility = ShaderVisibility::Vertex;
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/key", changed));
    }
    {
        RenderBindingLayoutData changed = base;
        changed.constant_fields.push_back(
            { .name = "tint", .type = RenderBindingConstantType::Color });
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/key", changed));
    }
    {
        RenderBindingLayoutData changed = base;
        changed.bindings[0].visibility = ShaderVisibility::All;
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/key", changed));
    }
    {
        RenderBindingLayoutData changed = base;
        changed.samplers[0].kind = StaticSamplerKind::LinearWrap;
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/key", changed));
    }
    {
        // Same content, same name -> same key (content addressing).
        EXPECT_TRUE(
            base_key == make_render_binding_layout_key("layout/key", base));
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/other", base));
    }
}
