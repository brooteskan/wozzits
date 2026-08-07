#include "wz_test.h"

#include <wozzits/rhi/compute_program.h>

using namespace wz::rhi;

namespace
{
    // A landscape-field-CS-shaped program: 64-wide thread group, one root
    // constant (vertex count).
    ComputeProgramDesc make_landscape_field(
        ConstantSemanticRegistry& constants)
    {
        ComputeProgramDesc d;
        d.name = "landscape_field";
        d.compute_shader = "shaders/compute/landscape_field_cs.hlsl";
        d.thread_group_size[0] = 64;
        ShaderResourceGroupLayout object_srg;
        object_srg.binding_slot = 2;
        object_srg.constants_binding = RootConstantsBinding{
            ShaderStage::Compute,
            0,
            0 };
        WZ_CHECK(object_srg.constants.append(
            constants.acquire("dispatch_constants"),
            sizeof(uint32_t)));
        d.shader_resource_groups.push_back(object_srg);
        return d;
    }

    const ShaderResourceGroupLayout* object_srg(const ComputeProgramDesc& desc)
    {
        return find_shader_resource_group_layout(
            desc.shader_resource_groups,
            2);
    }
}

static void register_then_get_round_trips()
{
    ConstantSemanticRegistry constants;
    ComputeProgramRegistry registry;
    const Tag tag = registry.register_program(make_landscape_field(constants));
    WZ_CHECK(tag.valid());

    const ComputeProgramDesc* desc = registry.get(tag);
    WZ_CHECK(desc != nullptr);
    if (desc) {
        WZ_CHECK_EQ(desc->name, std::string{ "landscape_field" });
        WZ_CHECK_EQ(desc->thread_group_size[0], 64u);
        const ShaderResourceGroupLayout* srg = object_srg(*desc);
        WZ_CHECK(srg != nullptr);
        if (srg) {
            WZ_CHECK_EQ(srg->constants.dword_count(), 1u);
            WZ_CHECK(srg->constants_binding.visibility
                == ShaderStage::Compute);
        }
    }
}

// Same anti-#167 invariant as render programs: an unregistered compute program
// is a checkable miss, not a silent fallthrough.
static void unregistered_is_a_checkable_miss()
{
    ConstantSemanticRegistry constants;
    ComputeProgramRegistry registry;
    registry.register_program(make_landscape_field(constants));
    const Tag missing = registry.find("not_registered");
    WZ_CHECK_FALSE(missing.valid());
    WZ_CHECK(registry.get(missing) == nullptr);
}

// Compute binds UAVs; the binding's semantic resolves to a Tag like any other
// (the open identity set, shared with render programs).
static void uav_binding_semantic_is_a_tag()
{
    DescriptorSemanticRegistry semantics;
    ConstantSemanticRegistry constants;
    ComputeProgramDesc d = make_landscape_field(constants);
    ShaderResourceGroupLayout* srg = d.shader_resource_groups.empty()
        ? nullptr
        : &d.shader_resource_groups[0];
    WZ_CHECK(srg != nullptr);
    if (srg) {
        srg->descriptors.push_back(DescriptorBinding{
            DescriptorKind::UAV,
            ShaderStage::Compute,
            semantics.acquire("mesh_field_output"),
            0,
            0,
            1 });
    }

    ComputeProgramRegistry registry;
    const Tag tag = registry.register_program(d);
    const ComputeProgramDesc* out = registry.get(tag);
    WZ_CHECK(out != nullptr);
    if (out) {
        const ShaderResourceGroupLayout* out_srg = object_srg(*out);
        WZ_CHECK(out_srg != nullptr);
        if (out_srg) {
            WZ_CHECK_EQ(
                out_srg->descriptors.size(),
                static_cast<size_t>(1));
            WZ_CHECK(out_srg->descriptors[0].kind == DescriptorKind::UAV);
            WZ_CHECK(out_srg->descriptors[0].semantic.valid());
            WZ_CHECK_EQ(semantics.name_of(out_srg->descriptors[0].semantic),
                std::string_view{ "mesh_field_output" });
        }
    }
}

static void reregister_updates_in_place()
{
    ConstantSemanticRegistry constants;
    ComputeProgramRegistry registry;
    const Tag first = registry.register_program(make_landscape_field(constants));

    ComputeProgramDesc updated = make_landscape_field(constants);
    updated.thread_group_size[0] = 128;
    const Tag again = registry.register_program(updated);

    WZ_CHECK(first == again);
    WZ_CHECK_EQ(registry.size(), 1u);
    const ComputeProgramDesc* desc = registry.get(again);
    if (desc) {
        WZ_CHECK_EQ(desc->thread_group_size[0], 128u);
    }
}

int main()
{
    WZ_RUN(register_then_get_round_trips);
    WZ_RUN(unregistered_is_a_checkable_miss);
    WZ_RUN(uav_binding_semantic_is_a_tag);
    WZ_RUN(reregister_updates_in_place);
    WZ_TEST_RETURN();
}
