#include "wz_test.h"

#include <wozzits/rhi/render_program.h>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace wz::rhi;

static void appends_named_intervals_into_packed_layout()
{
    ConstantSemanticRegistry semantics;
    const Tag world = semantics.acquire("world");
    const Tag view_proj = semantics.acquire("view_proj");
    const Tag style = semantics.acquire("style");

    ConstantsLayout layout;
    WZ_CHECK(layout.append(world, 64));
    WZ_CHECK(layout.append(view_proj, 64));
    WZ_CHECK(layout.append(style, 16));

    WZ_CHECK_EQ(layout.byte_size(), 144u);
    WZ_CHECK_EQ(layout.dword_count(), 36u);

    const auto world_interval = layout.find(world);
    const auto view_interval = layout.find(view_proj);
    const auto style_interval = layout.find(style);
    WZ_CHECK(world_interval.has_value());
    WZ_CHECK(view_interval.has_value());
    WZ_CHECK(style_interval.has_value());
    if (world_interval && view_interval && style_interval) {
        WZ_CHECK_EQ(world_interval->byte_offset, 0u);
        WZ_CHECK_EQ(view_interval->byte_offset, 64u);
        WZ_CHECK_EQ(style_interval->byte_offset, 128u);
        WZ_CHECK_EQ(style_interval->byte_size, 16u);
    }
}

static void rejects_null_duplicate_and_unaligned_intervals()
{
    ConstantSemanticRegistry semantics;
    const Tag world = semantics.acquire("world");

    ConstantsLayout layout;
    WZ_CHECK_FALSE(layout.append({}, 64));
    WZ_CHECK_FALSE(layout.append(world, 0));
    WZ_CHECK_FALSE(layout.append(world, 6));
    WZ_CHECK(layout.append(world, 64));
    WZ_CHECK_FALSE(layout.append(world, 64));
    WZ_CHECK_EQ(layout.size(), static_cast<size_t>(1));
}

static void render_program_root_constants_build_layout()
{
    ConstantSemanticRegistry semantics;
    const Tag object_constants = semantics.acquire("object_constants");
    const Tag style_constants = semantics.acquire("style_constants");

    std::vector<RootConstantBinding> bindings;
    bindings.push_back(RootConstantBinding{
        ShaderStage::All, object_constants, 0, 0, 32 });
    bindings.push_back(RootConstantBinding{
        ShaderStage::Pixel, style_constants, 1, 0, 4 });

    const auto layout = make_constants_layout(bindings);
    WZ_CHECK(layout.has_value());
    if (!layout) {
        return;
    }

    WZ_CHECK_EQ(layout->byte_size(), 144u);
    const auto object = layout->find(object_constants);
    const auto style = layout->find(style_constants);
    WZ_CHECK(object.has_value());
    WZ_CHECK(style.has_value());
    if (object && style) {
        WZ_CHECK_EQ(object->dword_offset(), 0u);
        WZ_CHECK_EQ(object->dword_count(), 32u);
        WZ_CHECK_EQ(style->dword_offset(), 32u);
        WZ_CHECK_EQ(style->dword_count(), 4u);
    }
}

static void payload_is_packed_bytes_after_layout_resolution()
{
    ConstantSemanticRegistry semantics;
    const Tag style = semantics.acquire("style");

    ConstantsLayout layout;
    WZ_CHECK(layout.append(style, 16));
    const auto interval = layout.find(style);
    WZ_CHECK(interval.has_value());
    if (!interval) {
        return;
    }

    std::vector<uint8_t> payload(layout.byte_size());
    const uint32_t rgba[4] = { 1u, 2u, 3u, 4u };
    std::memcpy(
        payload.data() + interval->byte_offset,
        rgba,
        sizeof(rgba));

    const uint32_t* packed =
        reinterpret_cast<const uint32_t*>(
            payload.data() + interval->byte_offset);
    WZ_CHECK_EQ(packed[0], 1u);
    WZ_CHECK_EQ(packed[3], 4u);
}

int main()
{
    WZ_RUN(appends_named_intervals_into_packed_layout);
    WZ_RUN(rejects_null_duplicate_and_unaligned_intervals);
    WZ_RUN(render_program_root_constants_build_layout);
    WZ_RUN(payload_is_packed_bytes_after_layout_resolution);
    WZ_TEST_RETURN();
}
