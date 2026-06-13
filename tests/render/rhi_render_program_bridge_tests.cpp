#include <gtest/gtest.h>

#include <engine/rendering/rhi_render_program_bridge.h>

namespace ea = wz::engine::assets;

namespace
{
    // A mesh_mask_style-shaped authored program: t0 = field values SRV,
    // t1 = mask rules SRV, plus the 48-DWORD root constant block.
    ea::CustomRenderProgramDesc make_mask_style_desc()
    {
        ea::CustomRenderProgramDesc src;
        src.name = "mesh_mask_style";
        src.binding_model = ea::RenderBindingModel::MeshIA;
        src.topology = ea::RenderPrimitiveTopology::TriangleList;
        src.input_layout = ea::InputLayoutKind::MeshPositionNormalUV;
        src.blend_mode = ea::BlendMode::Opaque;
        src.depth_mode = ea::DepthMode::TestWrite;
        src.raster_mode = ea::RasterMode::SolidCullBack;
        src.root_constants.push_back(
            ea::RootConstantBinding{ ea::ShaderVisibility::All, 0, 0, 48 });
        src.descriptor_bindings.push_back(ea::DescriptorBinding{
            ea::DescriptorKind::StructuredBufferSRV, ea::ShaderVisibility::Pixel,
            ea::DescriptorSemantic::MeshFieldVisualization, 0, 0, 1 });
        src.descriptor_bindings.push_back(ea::DescriptorBinding{
            ea::DescriptorKind::StructuredBufferSRV, ea::ShaderVisibility::Pixel,
            ea::DescriptorSemantic::MeshMaskRules, 1, 0, 1 });
        return src;
    }
}

TEST(RhiRenderProgramBridge, ConvertsDeclarativeStateAndDescriptors)
{
    const ea::CustomRenderProgramDesc src = make_mask_style_desc();
    wz::rhi::DescriptorSemanticRegistry semantics;

    const wz::rhi::RenderProgramDesc out =
        wz::engine::rendering::to_rhi_render_program_desc(src, semantics);

    EXPECT_EQ(out.name, "mesh_mask_style");
    EXPECT_EQ(out.vertex_source, wz::rhi::VertexSource::InputAssembler);
    ASSERT_EQ(out.vertex_layout.attributes.size(), 3u);
    EXPECT_EQ(out.vertex_layout.attributes[0].format,
        wz::rhi::VertexFormat::Float32x3);
    EXPECT_EQ(out.vertex_layout.attributes[0].offset, 0u);
    EXPECT_EQ(out.vertex_layout.attributes[1].format,
        wz::rhi::VertexFormat::Float32x3);
    EXPECT_EQ(out.vertex_layout.attributes[1].offset, 12u);
    EXPECT_EQ(out.vertex_layout.attributes[2].format,
        wz::rhi::VertexFormat::Float32x2);
    EXPECT_EQ(out.vertex_layout.attributes[2].offset, 24u);
    EXPECT_EQ(out.blend_mode, wz::rhi::BlendMode::Opaque);
    EXPECT_EQ(out.depth_mode, wz::rhi::DepthMode::TestWrite);
    EXPECT_EQ(out.raster_mode, wz::rhi::RasterMode::SolidCullBack);

    ASSERT_EQ(out.root_constants.size(), 1u);
    EXPECT_EQ(out.root_constants[0].value_count, 48u);

    ASSERT_EQ(out.descriptor_bindings.size(), 2u);
    EXPECT_EQ(out.descriptor_bindings[0].shader_register, 0u);
    EXPECT_EQ(out.descriptor_bindings[1].shader_register, 1u);
}

// The headline conversion: the engine's DescriptorSemantic enum becomes
// registered rhi Tags, resolvable by name.
TEST(RhiRenderProgramBridge, DescriptorSemanticsBecomeRegisteredTags)
{
    const ea::CustomRenderProgramDesc src = make_mask_style_desc();
    wz::rhi::DescriptorSemanticRegistry semantics;

    const wz::rhi::RenderProgramDesc out =
        wz::engine::rendering::to_rhi_render_program_desc(src, semantics);

    ASSERT_EQ(out.descriptor_bindings.size(), 2u);
    const wz::rhi::Tag t0 = out.descriptor_bindings[0].semantic;
    const wz::rhi::Tag t1 = out.descriptor_bindings[1].semantic;

    EXPECT_TRUE(t0.valid());
    EXPECT_TRUE(t1.valid());
    EXPECT_FALSE(t0 == t1);
    EXPECT_EQ(semantics.name_of(t0), std::string_view{ "mesh_field_visualization" });
    EXPECT_EQ(semantics.name_of(t1), std::string_view{ "mesh_mask_rules" });
}

// Two bindings with the same semantic resolve to the same Tag (the registry
// dedups by name).
TEST(RhiRenderProgramBridge, RepeatedSemanticDedupsToSameTag)
{
    ea::CustomRenderProgramDesc src;
    src.name = "twin";
    src.descriptor_bindings.push_back(ea::DescriptorBinding{
        ea::DescriptorKind::StructuredBufferSRV, ea::ShaderVisibility::Pixel,
        ea::DescriptorSemantic::MeshFieldVisualization, 0, 0, 1 });
    src.descriptor_bindings.push_back(ea::DescriptorBinding{
        ea::DescriptorKind::TextureSRV, ea::ShaderVisibility::Pixel,
        ea::DescriptorSemantic::MeshFieldVisualization, 1, 0, 1 });

    wz::rhi::DescriptorSemanticRegistry semantics;
    const wz::rhi::RenderProgramDesc out =
        wz::engine::rendering::to_rhi_render_program_desc(src, semantics);

    ASSERT_EQ(out.descriptor_bindings.size(), 2u);
    EXPECT_TRUE(out.descriptor_bindings[0].semantic
        == out.descriptor_bindings[1].semantic);
}
