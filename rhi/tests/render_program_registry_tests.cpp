#include "wz_test.h"

#include <wozzits/rhi/render_program_registry.h>

#include <span>

using wz::rhi::RenderProgramDesc;
using wz::rhi::RenderProgramRegistry;
using wz::rhi::RootConstantBinding;
using wz::rhi::ShaderResourceGroupLayout;
using wz::rhi::ShaderStage;
using wz::rhi::Tag;

static RenderProgramDesc make_desc(
    wz::rhi::ConstantSemanticRegistry& constants,
    const char* name,
    uint32_t root_values = 40)
{
    RenderProgramDesc desc;
    desc.name = name;
    desc.vertex_shader = std::string(name) + "_vs.hlsl";
    desc.pixel_shader = std::string(name) + "_ps.hlsl";
    const RootConstantBinding object_constants{
        ShaderStage::All,
        constants.acquire("object_constants"),
        /*shader_register*/ 0, /*register_space*/ 0,
        /*value_count*/ root_values };
    const auto constants_layout =
        wz::rhi::make_constants_layout(
            std::span<const RootConstantBinding>{ &object_constants, 1 });
    WZ_CHECK(constants_layout.has_value());

    ShaderResourceGroupLayout object_srg;
    object_srg.binding_slot = 2;
    if (constants_layout) {
        object_srg.constants = *constants_layout;
    }
    desc.shader_resource_groups.push_back(object_srg);
    return desc;
}

static void register_then_get_round_trips()
{
    wz::rhi::ConstantSemanticRegistry constants;
    RenderProgramRegistry registry;
    const Tag tag =
        registry.register_program(make_desc(constants, "mesh_mask_style"));
    WZ_CHECK(tag.valid());

    const RenderProgramDesc* desc = registry.get(tag);
    WZ_CHECK(desc != nullptr);
    if (desc) {
        WZ_CHECK_EQ(desc->name, std::string{ "mesh_mask_style" });
        WZ_CHECK_EQ(desc->shader_resource_groups.size(), static_cast<size_t>(1));
        if (!desc->shader_resource_groups.empty()) {
            WZ_CHECK_EQ(desc->shader_resource_groups[0].binding_slot, 2u);
            WZ_CHECK_EQ(desc->shader_resource_groups[0].constants.dword_count(), 40u);
        }
    }
}

// THE anti-#167 invariant, stated as a test: resolving a program that was
// never registered is a checkable miss (null Tag / nullptr), not a silent
// fallthrough that renders nothing. This is the bug that motivated the repo.
static void unregistered_program_is_a_checkable_miss()
{
    wz::rhi::ConstantSemanticRegistry constants;
    RenderProgramRegistry registry;
    registry.register_program(make_desc(constants, "mesh_surface"));

    // Asking for a program nobody registered yields a null Tag...
    const Tag missing = registry.find("mesh_mask_style");
    WZ_CHECK_FALSE(missing.valid());

    // ...and get() on that null Tag is nullptr, which a caller MUST handle —
    // there is no enum case to silently fall through.
    WZ_CHECK(registry.get(missing) == nullptr);
}

static void find_resolves_registered_program_by_name()
{
    wz::rhi::ConstantSemanticRegistry constants;
    RenderProgramRegistry registry;
    const Tag registered =
        registry.register_program(make_desc(constants, "mesh_surface"));
    const Tag found = registry.find("mesh_surface");
    WZ_CHECK(found == registered);
}

static void reregister_updates_in_place()
{
    wz::rhi::ConstantSemanticRegistry constants;
    RenderProgramRegistry registry;
    const Tag first =
        registry.register_program(make_desc(constants, "mesh_surface"));

    const Tag again =
        registry.register_program(make_desc(constants, "mesh_surface", 48));

    WZ_CHECK(first == again);                 // same identity
    WZ_CHECK_EQ(registry.size(), 1u);          // not duplicated
    const RenderProgramDesc* desc = registry.get(again);
    WZ_CHECK(desc != nullptr);
    if (desc && !desc->shader_resource_groups.empty()) {
        WZ_CHECK_EQ(desc->shader_resource_groups[0].constants.dword_count(), 48u);
    }
}

// The basis for "the editor realizes a pipeline for every program that exists"
// rather than a hand-maintained list that can fall behind: enumeration.
static void visit_enumerates_every_registered_program()
{
    wz::rhi::ConstantSemanticRegistry constants;
    RenderProgramRegistry registry;
    registry.register_program(make_desc(constants, "mesh_surface"));
    registry.register_program(make_desc(constants, "mesh_wireframe"));
    registry.register_program(make_desc(constants, "mesh_mask_style"));

    size_t seen = 0;
    registry.visit([&](Tag tag, const RenderProgramDesc& desc) {
        WZ_CHECK(tag.valid());
        WZ_CHECK_FALSE(desc.name.empty());
        ++seen;
    });
    WZ_CHECK_EQ(seen, static_cast<size_t>(3));
}

// clear() retires every registered program at once (the asset-graph-swap
// reset): size() drops to zero, prior Tags miss, and the registry is reusable.
static void clear_drops_all_programs()
{
    wz::rhi::ConstantSemanticRegistry constants;
    RenderProgramRegistry registry;
    const Tag a = registry.register_program(make_desc(constants, "mesh_surface"));
    const Tag b = registry.register_program(make_desc(constants, "mesh_wireframe"));
    WZ_CHECK_EQ(registry.size(), static_cast<size_t>(2));

    registry.clear();
    WZ_CHECK_EQ(registry.size(), static_cast<size_t>(0));
    WZ_CHECK_FALSE(registry.find("mesh_surface").valid());
    WZ_CHECK(registry.get(a) == nullptr);
    WZ_CHECK(registry.get(b) == nullptr);

    // Reusable after clear: a fresh registration resolves cleanly.
    const Tag c = registry.register_program(make_desc(constants, "mesh_surface"));
    WZ_CHECK(c.valid());
    WZ_CHECK_EQ(registry.size(), static_cast<size_t>(1));
    WZ_CHECK(registry.get(c) != nullptr);
}

// release() retires a SINGLE program (the survivor-preserving reconcile an
// asset-graph swap needs), as opposed to clear() which retires all. The
// survivor's Tag stays valid; the released Tag misses; the freed slot is
// reusable; and releasing an already-released / null Tag is a safe no-op.
static void release_retires_one_program_and_keeps_survivors()
{
    wz::rhi::ConstantSemanticRegistry constants;
    RenderProgramRegistry registry;
    const Tag a = registry.register_program(make_desc(constants, "mesh_surface"));
    const Tag b = registry.register_program(make_desc(constants, "mesh_wireframe"));
    WZ_CHECK_EQ(registry.size(), static_cast<size_t>(2));

    registry.release(a);
    WZ_CHECK_EQ(registry.size(), static_cast<size_t>(1));
    WZ_CHECK(registry.get(a) == nullptr);
    WZ_CHECK_FALSE(registry.find("mesh_surface").valid());
    WZ_CHECK(registry.get(b) != nullptr);
    WZ_CHECK(registry.find("mesh_wireframe") == b);

    // Over-release of a retired Tag and release of a null Tag are no-ops: they
    // must not corrupt the survivor or the slot count.
    registry.release(a);
    registry.release(Tag{});
    WZ_CHECK_EQ(registry.size(), static_cast<size_t>(1));
    WZ_CHECK(registry.get(b) != nullptr);

    // The freed slot is reusable by a fresh registration.
    const Tag c =
        registry.register_program(make_desc(constants, "mesh_mask_style"));
    WZ_CHECK(c.valid());
    WZ_CHECK_EQ(registry.size(), static_cast<size_t>(2));
    WZ_CHECK(registry.get(c) != nullptr);
}

int main()
{
    WZ_RUN(register_then_get_round_trips);
    WZ_RUN(unregistered_program_is_a_checkable_miss);
    WZ_RUN(find_resolves_registered_program_by_name);
    WZ_RUN(reregister_updates_in_place);
    WZ_RUN(visit_enumerates_every_registered_program);
    WZ_RUN(clear_drops_all_programs);
    WZ_RUN(release_retires_one_program_and_keeps_survivors);
    WZ_TEST_RETURN();
}
