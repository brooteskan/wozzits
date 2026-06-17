#include <gtest/gtest.h>

#include "fake_gpu_backend.h"

#include <engine/rendering/rhi_context.h>
#include <engine/rendering/rhi_render_program_bridge.h>
#include <engine/rendering/rhi_shader_bridge.h>

#include <wozzits/rhi/shader_module.h>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string_view>

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
            ea::RootConstantBinding{
                ea::ShaderVisibility::All, 0, 2, 48, "object_constants" });
        src.descriptor_bindings.push_back(ea::DescriptorBinding{
            ea::DescriptorKind::StructuredBufferSRV, ea::ShaderVisibility::Pixel,
            ea::DescriptorSemantic::MeshFieldVisualization, 0, 2, 1 });
        src.descriptor_bindings.push_back(ea::DescriptorBinding{
            ea::DescriptorKind::StructuredBufferSRV, ea::ShaderVisibility::Pixel,
            ea::DescriptorSemantic::MeshMaskRules, 1, 2, 1 });
        return src;
    }

    wz::asset::AssetKey shader_key(uint64_t lo, uint64_t hi)
    {
        wz::asset::AssetKey key;
        key.content_hash.lo = lo;
        key.content_hash.hi = hi;
        return key;
    }
}

TEST(RhiRenderProgramBridge, ConvertsDeclarativeStateAndDescriptors)
{
    const ea::CustomRenderProgramDesc src = make_mask_style_desc();
    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry constants;

    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            src, descriptors, constants);
    ASSERT_TRUE(converted.has_value());
    const wz::rhi::RenderProgramDesc& out = *converted;

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

    const wz::rhi::ShaderResourceGroupLayout* object_layout =
        wz::rhi::find_shader_resource_group_layout(
            out.shader_resource_groups, 2);
    ASSERT_NE(object_layout, nullptr);
    EXPECT_EQ(object_layout->constants.dword_count(), 48u);
    EXPECT_EQ(object_layout->constants_binding.shader_register, 0u);
    EXPECT_EQ(object_layout->constants_binding.register_space, 2u);
    EXPECT_EQ(object_layout->constants_binding.visibility,
        wz::rhi::ShaderStage::All);
    ASSERT_EQ(object_layout->descriptors.size(), 2u);
    EXPECT_EQ(object_layout->descriptors[0].shader_register, 0u);
    EXPECT_EQ(object_layout->descriptors[1].shader_register, 1u);
}

// The headline conversion: the engine's DescriptorSemantic enum becomes
// registered rhi Tags, resolvable by name.
TEST(RhiRenderProgramBridge, DescriptorSemanticsBecomeRegisteredTags)
{
    const ea::CustomRenderProgramDesc src = make_mask_style_desc();
    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry constants;

    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            src, descriptors, constants);
    ASSERT_TRUE(converted.has_value());
    const wz::rhi::RenderProgramDesc& out = *converted;

    const wz::rhi::ShaderResourceGroupLayout* object_layout =
        wz::rhi::find_shader_resource_group_layout(
            out.shader_resource_groups, 2);
    ASSERT_NE(object_layout, nullptr);
    ASSERT_EQ(object_layout->descriptors.size(), 2u);
    const wz::rhi::Tag t0 = object_layout->descriptors[0].semantic;
    const wz::rhi::Tag t1 = object_layout->descriptors[1].semantic;

    EXPECT_TRUE(t0.valid());
    EXPECT_TRUE(t1.valid());
    EXPECT_FALSE(t0 == t1);
    EXPECT_EQ(descriptors.name_of(t0), std::string_view{ "mesh_field_visualization" });
    EXPECT_EQ(descriptors.name_of(t1), std::string_view{ "mesh_mask_rules" });
    const wz::rhi::Tag object_constants = constants.find("object_constants");
    EXPECT_TRUE(object_constants.valid());
    EXPECT_TRUE(object_layout->constants.find(object_constants).has_value());
    EXPECT_EQ(constants.name_of(object_constants),
        std::string_view{ "object_constants" });
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

    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry constants;
    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            src, descriptors, constants);
    ASSERT_TRUE(converted.has_value());
    const wz::rhi::RenderProgramDesc& out = *converted;

    const wz::rhi::ShaderResourceGroupLayout* layout =
        wz::rhi::find_shader_resource_group_layout(
            out.shader_resource_groups, 0);
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->descriptors.size(), 2u);
    EXPECT_TRUE(layout->descriptors[0].semantic
        == layout->descriptors[1].semantic);
}

TEST(RhiRenderProgramBridge, MeshVertexPullMapsToPullSourceAndTags)
{
    ea::CustomRenderProgramDesc src;
    src.name = "mesh_vertex_pull";
    src.binding_model = ea::RenderBindingModel::MeshVertexPull;
    src.topology = ea::RenderPrimitiveTopology::TriangleList;
    src.input_layout = ea::InputLayoutKind::None;
    src.descriptor_bindings.push_back(ea::DescriptorBinding{
        ea::DescriptorKind::StructuredBufferSRV, ea::ShaderVisibility::Vertex,
        ea::DescriptorSemantic::PulledMeshPositions, 0, 0, 1 });
    src.descriptor_bindings.push_back(ea::DescriptorBinding{
        ea::DescriptorKind::StructuredBufferSRV, ea::ShaderVisibility::Vertex,
        ea::DescriptorSemantic::PulledMeshIndices, 1, 0, 1 });

    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry constants;
    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            src, descriptors, constants);
    ASSERT_TRUE(converted.has_value());
    const wz::rhi::RenderProgramDesc& out = *converted;

    EXPECT_EQ(out.vertex_source, wz::rhi::VertexSource::Pull);
    EXPECT_TRUE(out.vertex_layout.attributes.empty());
    const wz::rhi::ShaderResourceGroupLayout* layout =
        wz::rhi::find_shader_resource_group_layout(
            out.shader_resource_groups, 0);
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->descriptors.size(), 2u);
    EXPECT_EQ(
        descriptors.name_of(layout->descriptors[0].semantic),
        std::string_view{ "pulled_mesh_positions" });
    EXPECT_EQ(
        descriptors.name_of(layout->descriptors[1].semantic),
        std::string_view{ "pulled_mesh_indices" });
}

TEST(RhiRenderProgramBridge, EmptyRootConstantSemanticIsMalformed)
{
    ea::CustomRenderProgramDesc src = make_mask_style_desc();
    src.root_constants[0].semantic.clear();

    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry constants;
    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            src, descriptors, constants);

    EXPECT_FALSE(converted.has_value());
}

TEST(RhiRenderProgramBridge, UnknownDescriptorSemanticIsMalformed)
{
    ea::CustomRenderProgramDesc src = make_mask_style_desc();
    src.descriptor_bindings[0].semantic = ea::DescriptorSemantic::Unknown;

    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry constants;
    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            src, descriptors, constants);

    EXPECT_FALSE(converted.has_value());
}

TEST(RhiRenderProgramBridge, ConvertsResolvedRenderProgramDataWithShaderKeys)
{
    const ea::CustomRenderProgramDesc src = make_mask_style_desc();
    ea::RenderProgramData data;
    data.binding_model = src.binding_model;
    data.topology = src.topology;
    data.input_layout = src.input_layout;
    data.blend_mode = src.blend_mode;
    data.depth_mode = src.depth_mode;
    data.raster_mode = src.raster_mode;
    data.root_constants = src.root_constants;
    data.descriptor_bindings = src.descriptor_bindings;

    const wz::asset::AssetKey program_key = shader_key(0xCC00, 0xDD00);
    const wz::asset::AssetKey vertex_key = shader_key(0xAA01, 0xBB01);
    const wz::asset::AssetKey pixel_key = shader_key(0xAA02, 0xBB02);

    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry constants;
    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            data,
            program_key,
            vertex_key,
            pixel_key,
            descriptors,
            constants);

    ASSERT_TRUE(converted.has_value());
    EXPECT_EQ(converted->name, "program#000000000000dd00000000000000cc00");
    EXPECT_EQ(converted->vertex_shader,
        wz::engine::rendering::shader_ref(vertex_key));
    EXPECT_EQ(converted->pixel_shader,
        wz::engine::rendering::shader_ref(pixel_key));

    const wz::rhi::ShaderResourceGroupLayout* object_layout =
        wz::rhi::find_shader_resource_group_layout(
            converted->shader_resource_groups, 2);
    ASSERT_NE(object_layout, nullptr);
    EXPECT_EQ(object_layout->constants.dword_count(), 48u);
    ASSERT_EQ(object_layout->descriptors.size(), 2u);
    EXPECT_EQ(
        descriptors.name_of(object_layout->descriptors[0].semantic),
        std::string_view{ "mesh_field_visualization" });
}

TEST(RhiShaderBridge, RegisteredProgramShadersResolveThroughConvertedProgram)
{
    ea::CustomRenderProgramDesc src = make_mask_style_desc();
    src.vertex_shader = shader_key(0xAA01, 0xBB01);
    src.pixel_shader = shader_key(0xAA02, 0xBB02);

    wz::engine::rendering::test::FakeGpuBackend backend;
    wz::engine::rendering::RhiContext ctx(backend);

    const std::array<uint8_t, 4> vertex_bytecode = { 1, 2, 3, 4 };
    const std::array<uint8_t, 3> pixel_bytecode = { 5, 6, 7 };
    ASSERT_TRUE(wz::engine::rendering::register_program_shaders(
        ctx,
        src,
        vertex_bytecode,
        pixel_bytecode));

    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            src,
            ctx.descriptor_semantics,
            ctx.constant_semantics);
    ASSERT_TRUE(converted.has_value());

    const std::optional<wz::rhi::ProgramBytecode> resolved =
        wz::rhi::resolve_program_bytecode(*converted, ctx.shaders);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->vertex.size(), vertex_bytecode.size());
    EXPECT_TRUE(std::equal(
        resolved->vertex.begin(),
        resolved->vertex.end(),
        vertex_bytecode.begin()));
    EXPECT_EQ(resolved->pixel.size(), pixel_bytecode.size());
    EXPECT_TRUE(std::equal(
        resolved->pixel.begin(),
        resolved->pixel.end(),
        pixel_bytecode.begin()));
}
