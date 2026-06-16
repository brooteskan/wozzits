#include "wz_test.h"

#include <wozzits/rhi/draw_item.h>
#include <wozzits/rhi/draw_list_tag.h>

#include <string_view>

using namespace wz::rhi;

static void draw_list_tags_are_open_registered_pass_routes()
{
    DrawListTagRegistry registry;
    const DrawListTag depth = registry.acquire("depth");
    const DrawListTag forward = registry.acquire("forward");
    const DrawListTag shadow = registry.acquire("shadow");
    const DrawListTag debug = registry.acquire("debug");

    WZ_CHECK(depth.valid());
    WZ_CHECK(forward.valid());
    WZ_CHECK(shadow.valid());
    WZ_CHECK(debug.valid());
    WZ_CHECK_FALSE(depth == forward);
    WZ_CHECK_EQ(registry.name_of(shadow), std::string_view{ "shadow" });
}

static void draw_list_mask_uses_tag_indices_and_rejects_null()
{
    DrawListTagRegistry registry;
    const DrawListTag depth = registry.acquire("depth");
    const DrawListTag forward = registry.acquire("forward");

    DrawListMask mask;
    WZ_CHECK(mask.empty());
    WZ_CHECK(mask.add(depth));
    WZ_CHECK(mask.add(forward));
    WZ_CHECK(mask.contains(depth));
    WZ_CHECK(mask.contains(forward));
    WZ_CHECK_FALSE(mask.contains(DrawListTag{}));
    WZ_CHECK_FALSE(mask.add(DrawListTag{}));

    WZ_CHECK(mask.remove(depth));
    WZ_CHECK_FALSE(mask.contains(depth));
    WZ_CHECK(mask.contains(forward));
}

static void masks_intersect_by_draw_list_membership()
{
    DrawListTagRegistry registry;
    const DrawListTag depth = registry.acquire("depth");
    const DrawListTag forward = registry.acquire("forward");
    const DrawListTag debug = registry.acquire("debug");

    DrawListMask depth_and_forward;
    WZ_CHECK(depth_and_forward.add(depth));
    WZ_CHECK(depth_and_forward.add(forward));

    DrawListMask forward_only = DrawListMask::from(forward);
    DrawListMask debug_only = DrawListMask::from(debug);

    WZ_CHECK(depth_and_forward.intersects(forward_only));
    WZ_CHECK_FALSE(depth_and_forward.intersects(debug_only));
}

static void draw_item_keeps_domain_and_pass_as_separate_axes()
{
    DrawListTagRegistry registry;
    const DrawListTag depth = registry.acquire("depth");
    const DrawListTag forward = registry.acquire("forward");

    DrawItem depth_item;
    depth_item.render_domain = RenderDomain::Opaque;
    depth_item.pass = depth;
    depth_item.filter_mask = DrawListMask::from(depth);

    DrawItem forward_item;
    forward_item.render_domain = RenderDomain::Opaque;
    forward_item.pass = forward;
    forward_item.filter_mask = DrawListMask::from(forward);

    WZ_CHECK(depth_item.render_domain == forward_item.render_domain);
    WZ_CHECK_FALSE(depth_item.pass == forward_item.pass);
    WZ_CHECK(depth_item.filter_mask.contains(depth));
    WZ_CHECK_FALSE(depth_item.filter_mask.contains(forward));
}

int main()
{
    WZ_RUN(draw_list_tags_are_open_registered_pass_routes);
    WZ_RUN(draw_list_mask_uses_tag_indices_and_rejects_null);
    WZ_RUN(masks_intersect_by_draw_list_membership);
    WZ_RUN(draw_item_keeps_domain_and_pass_as_separate_axes);
    WZ_TEST_RETURN();
}
