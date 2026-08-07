#include "wz_test.h"

#include <wozzits/rhi/shader_resource_group.h>

#include <array>
#include <cstdint>
#include <span>

using namespace wz::rhi;

namespace
{
    struct TestLayout
    {
        DescriptorSemanticRegistry descriptors;
        ConstantSemanticRegistry constants;
        Tag mesh_vertices{};
        Tag base_color{};
        Tag object_constants{};
        ShaderResourceGroupLayout layout{};
    };

    TestLayout make_object_layout()
    {
        TestLayout t;
        t.mesh_vertices = t.descriptors.acquire("mesh_vertices");
        t.base_color = t.descriptors.acquire("base_color_texture");
        t.object_constants = t.constants.acquire("object_constants");

        t.layout.binding_slot = 2;
        t.layout.descriptors.push_back(DescriptorBinding{
            DescriptorKind::StructuredBufferSRV,
            ShaderStage::Vertex,
            t.mesh_vertices,
            /*shader_register*/ 0,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });
        t.layout.descriptors.push_back(DescriptorBinding{
            DescriptorKind::TextureSRV,
            ShaderStage::Pixel,
            t.base_color,
            /*shader_register*/ 1,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });

        WZ_CHECK(t.layout.constants.append(t.object_constants, 16));
        return t;
    }
}

static void builds_from_layout_with_indexed_storage()
{
    TestLayout t = make_object_layout();
    ShaderResourceGroup srg(t.layout);

    WZ_CHECK_EQ(srg.binding_slot(), 2u);
    WZ_CHECK_EQ(srg.layout_hash(), t.layout.hash());
    WZ_CHECK_EQ(srg.resource_count(), static_cast<size_t>(2));
    WZ_CHECK_EQ(srg.constant_byte_size(), static_cast<size_t>(16));

    const auto vertex_index = srg.resolve_resource_index(t.mesh_vertices);
    const auto texture_index = srg.resolve_resource_index(t.base_color);
    WZ_CHECK(vertex_index.has_value());
    WZ_CHECK(texture_index.has_value());
    if (vertex_index && texture_index) {
        WZ_CHECK_EQ(*vertex_index, 0u);
        WZ_CHECK_EQ(*texture_index, 1u);
    }
}

static void set_by_tag_returns_index_then_index_path_sets()
{
    TestLayout t = make_object_layout();
    ShaderResourceGroup srg(t.layout);

    const GpuResourceHandle vertex_buffer{ 7, 1 };
    const GpuResourceHandle texture{ 8, 1 };

    const auto vertex_index = srg.set(t.mesh_vertices, vertex_buffer);
    WZ_CHECK(vertex_index.has_value());
    if (vertex_index) {
        WZ_CHECK_EQ(*vertex_index, 0u);
        WZ_CHECK(srg.resource(*vertex_index) == vertex_buffer);
    }

    WZ_CHECK(srg.set(1, texture));
    WZ_CHECK(srg.resource(1) == texture);
    WZ_CHECK_FALSE(srg.set(99, texture));
}

static void missing_resource_tag_is_null_index()
{
    TestLayout t = make_object_layout();
    ShaderResourceGroup srg(t.layout);

    const Tag missing = t.descriptors.find("not_in_layout");
    WZ_CHECK_FALSE(missing.valid());
    WZ_CHECK_FALSE(srg.resolve_resource_index(missing).has_value());
    WZ_CHECK_FALSE(srg.set(missing, GpuResourceHandle{ 9, 1 }).has_value());
}

static void constants_are_set_as_packed_bytes()
{
    TestLayout t = make_object_layout();
    ShaderResourceGroup srg(t.layout);

    const std::array<uint32_t, 4> rgba = { 1u, 2u, 3u, 4u };
    const auto interval = srg.set_constant(
        t.object_constants,
        std::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(rgba.data()),
            rgba.size() * sizeof(uint32_t) });

    WZ_CHECK(interval.has_value());
    if (!interval) {
        return;
    }
    WZ_CHECK_EQ(interval->byte_offset, 0u);
    WZ_CHECK_EQ(interval->byte_size, 16u);

    const uint32_t* packed =
        reinterpret_cast<const uint32_t*>(srg.constant_bytes().data());
    WZ_CHECK_EQ(packed[0], 1u);
    WZ_CHECK_EQ(packed[3], 4u);
}

static void satisfies_requires_slot_hash_and_all_resources()
{
    TestLayout t = make_object_layout();
    ShaderResourceGroup srg(t.layout);

    WZ_CHECK_FALSE(srg.satisfies(t.layout));

    WZ_CHECK(srg.set(t.mesh_vertices, GpuResourceHandle{ 10, 1 }).has_value());
    WZ_CHECK_FALSE(srg.satisfies(t.layout));

    WZ_CHECK(srg.set(t.base_color, GpuResourceHandle{ 11, 1 }).has_value());
    WZ_CHECK(srg.satisfies(t.layout));

    ShaderResourceGroupLayout wrong_slot = t.layout;
    wrong_slot.binding_slot = 1;
    WZ_CHECK_FALSE(srg.satisfies(wrong_slot));

    ShaderResourceGroupLayout changed_layout = t.layout;
    changed_layout.descriptors.push_back(DescriptorBinding{
        DescriptorKind::Sampler,
        ShaderStage::Pixel,
        t.descriptors.acquire("material_sampler"),
        /*shader_register*/ 2,
        /*register_space*/ 2,
        /*descriptor_count*/ 1 });
    WZ_CHECK_FALSE(srg.satisfies(changed_layout));
}

int main()
{
    WZ_RUN(builds_from_layout_with_indexed_storage);
    WZ_RUN(set_by_tag_returns_index_then_index_path_sets);
    WZ_RUN(missing_resource_tag_is_null_index);
    WZ_RUN(constants_are_set_as_packed_bytes);
    WZ_RUN(satisfies_requires_slot_hash_and_all_resources);
    WZ_TEST_RETURN();
}
