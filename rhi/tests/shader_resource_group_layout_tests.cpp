#include "wz_test.h"

#include <wozzits/rhi/render_program.h>
#include <wozzits/rhi/shader_resource_group_layout.h>

#include <span>
#include <string_view>

using namespace wz::rhi;

static void srg_layout_carries_slot_descriptors_and_constants()
{
    DescriptorSemanticRegistry descriptors;
    ConstantSemanticRegistry constants;

    const RootConstantBinding object_constants{
        ShaderStage::All,
        constants.acquire("object_constants"),
        /*shader_register*/ 0, /*register_space*/ 2,
        /*value_count*/ 32 };
    const auto constants_layout =
        make_constants_layout(
            std::span<const RootConstantBinding>{ &object_constants, 1 });
    WZ_CHECK(constants_layout.has_value());

    ShaderResourceGroupLayout object_srg;
    object_srg.binding_slot = 2;
    if (constants_layout) {
        object_srg.constants = *constants_layout;
    }
    object_srg.descriptors.push_back(DescriptorBinding{
        DescriptorKind::TextureSRV,
        ShaderStage::Pixel,
        descriptors.acquire("base_color_texture"),
        /*shader_register*/ 0,
        /*register_space*/ 2,
        /*descriptor_count*/ 1 });

    WZ_CHECK_EQ(object_srg.binding_slot, 2u);
    WZ_CHECK_EQ(object_srg.constants.dword_count(), 32u);
    WZ_CHECK_EQ(object_srg.descriptors.size(), static_cast<size_t>(1));
    WZ_CHECK_EQ(
        descriptors.name_of(object_srg.descriptors[0].semantic),
        std::string_view{ "base_color_texture" });
}

static void render_program_groups_layouts_by_binding_slot()
{
    RenderProgramDesc desc;
    desc.name = "mesh_surface";

    ShaderResourceGroupLayout view_srg;
    view_srg.binding_slot = 0;
    ShaderResourceGroupLayout material_srg;
    material_srg.binding_slot = 1;
    ShaderResourceGroupLayout object_srg;
    object_srg.binding_slot = 2;

    desc.shader_resource_groups.push_back(view_srg);
    desc.shader_resource_groups.push_back(material_srg);
    desc.shader_resource_groups.push_back(object_srg);

    WZ_CHECK(shader_resource_group_slots_are_unique(desc.shader_resource_groups));
    WZ_CHECK(find_shader_resource_group_layout(desc.shader_resource_groups, 0) != nullptr);
    WZ_CHECK(find_shader_resource_group_layout(desc.shader_resource_groups, 1) != nullptr);
    WZ_CHECK(find_shader_resource_group_layout(desc.shader_resource_groups, 2) != nullptr);
    WZ_CHECK(find_shader_resource_group_layout(desc.shader_resource_groups, 3) == nullptr);
}

static void duplicate_binding_slots_are_checkable()
{
    ShaderResourceGroupLayout first;
    first.binding_slot = 1;
    ShaderResourceGroupLayout duplicate;
    duplicate.binding_slot = 1;

    const ShaderResourceGroupLayout layouts[2] = { first, duplicate };
    WZ_CHECK_FALSE(shader_resource_group_slots_are_unique(layouts));
}

int main()
{
    WZ_RUN(srg_layout_carries_slot_descriptors_and_constants);
    WZ_RUN(render_program_groups_layouts_by_binding_slot);
    WZ_RUN(duplicate_binding_slots_are_checkable);
    WZ_TEST_RETURN();
}
