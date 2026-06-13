#include "wz_test.h"

#include <wozzits/rhi/render_program_registry.h>

using wz::rhi::RenderProgramDesc;
using wz::rhi::RenderProgramRegistry;
using wz::rhi::Tag;

static RenderProgramDesc make_desc(const char* name)
{
    RenderProgramDesc desc;
    desc.name = name;
    desc.vertex_shader = std::string(name) + "_vs.hlsl";
    desc.pixel_shader = std::string(name) + "_ps.hlsl";
    desc.root_constant_count = 40;
    return desc;
}

static void register_then_get_round_trips()
{
    RenderProgramRegistry registry;
    const Tag tag = registry.register_program(make_desc("mesh_mask_style"));
    WZ_CHECK(tag.valid());

    const RenderProgramDesc* desc = registry.get(tag);
    WZ_CHECK(desc != nullptr);
    if (desc) {
        WZ_CHECK_EQ(desc->name, std::string{ "mesh_mask_style" });
        WZ_CHECK_EQ(desc->root_constant_count, 40u);
    }
}

// THE anti-#167 invariant, stated as a test: resolving a program that was
// never registered is a checkable miss (null Tag / nullptr), not a silent
// fallthrough that renders nothing. This is the bug that motivated the repo.
static void unregistered_program_is_a_checkable_miss()
{
    RenderProgramRegistry registry;
    registry.register_program(make_desc("mesh_surface"));

    // Asking for a program nobody registered yields a null Tag...
    const Tag missing = registry.find("mesh_mask_style");
    WZ_CHECK_FALSE(missing.valid());

    // ...and get() on that null Tag is nullptr, which a caller MUST handle —
    // there is no enum case to silently fall through.
    WZ_CHECK(registry.get(missing) == nullptr);
}

static void find_resolves_registered_program_by_name()
{
    RenderProgramRegistry registry;
    const Tag registered = registry.register_program(make_desc("mesh_surface"));
    const Tag found = registry.find("mesh_surface");
    WZ_CHECK(found == registered);
}

static void reregister_updates_in_place()
{
    RenderProgramRegistry registry;
    const Tag first = registry.register_program(make_desc("mesh_surface"));

    RenderProgramDesc updated = make_desc("mesh_surface");
    updated.root_constant_count = 48;
    const Tag again = registry.register_program(updated);

    WZ_CHECK(first == again);                 // same identity
    WZ_CHECK_EQ(registry.size(), 1u);          // not duplicated
    const RenderProgramDesc* desc = registry.get(again);
    WZ_CHECK(desc != nullptr);
    if (desc) {
        WZ_CHECK_EQ(desc->root_constant_count, 48u);  // updated
    }
}

// The basis for "the editor realizes a pipeline for every program that exists"
// rather than a hand-maintained list that can fall behind: enumeration.
static void visit_enumerates_every_registered_program()
{
    RenderProgramRegistry registry;
    registry.register_program(make_desc("mesh_surface"));
    registry.register_program(make_desc("mesh_wireframe"));
    registry.register_program(make_desc("mesh_mask_style"));

    size_t seen = 0;
    registry.visit([&](Tag tag, const RenderProgramDesc& desc) {
        WZ_CHECK(tag.valid());
        WZ_CHECK_FALSE(desc.name.empty());
        ++seen;
    });
    WZ_CHECK_EQ(seen, static_cast<size_t>(3));
}

int main()
{
    WZ_RUN(register_then_get_round_trips);
    WZ_RUN(unregistered_program_is_a_checkable_miss);
    WZ_RUN(find_resolves_registered_program_by_name);
    WZ_RUN(reregister_updates_in_place);
    WZ_RUN(visit_enumerates_every_registered_program);
    WZ_TEST_RETURN();
}
