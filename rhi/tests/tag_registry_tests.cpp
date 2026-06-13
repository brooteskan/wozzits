#include "wz_test.h"

#include <wozzits/rhi/tag_registry.h>

using wz::rhi::Tag;
using wz::rhi::TagRegistry;

static void default_tag_is_null()
{
    Tag tag;
    WZ_CHECK_FALSE(tag.valid());
}

static void acquire_then_find_round_trips()
{
    TagRegistry<8> registry;
    const Tag forward = registry.acquire("Forward");
    WZ_CHECK(forward.valid());
    WZ_CHECK(registry.find("Forward") == forward);
    WZ_CHECK_EQ(registry.name_of(forward), std::string_view{ "Forward" });
}

// The anti-#167 invariant at the registry level: an unregistered name is a
// checkable null Tag, never a silent fallthrough.
static void missing_name_is_null_not_silent()
{
    TagRegistry<8> registry;
    (void)registry.acquire("Forward");
    const Tag missing = registry.find("Shadow");
    WZ_CHECK_FALSE(missing.valid());
}

static void distinct_names_get_distinct_tags()
{
    TagRegistry<8> registry;
    const Tag a = registry.acquire("Forward");
    const Tag b = registry.acquire("Shadow");
    WZ_CHECK(a.valid());
    WZ_CHECK(b.valid());
    WZ_CHECK_FALSE(a == b);
    WZ_CHECK_EQ(registry.allocated_count(), static_cast<size_t>(2));
}

static void same_name_ref_counts_to_one_slot()
{
    TagRegistry<8> registry;
    const Tag first = registry.acquire("Forward");
    const Tag second = registry.acquire("Forward");
    WZ_CHECK(first == second);
    WZ_CHECK_EQ(registry.allocated_count(), static_cast<size_t>(1));

    // One release still leaves a live reference.
    registry.release(second);
    WZ_CHECK(registry.find("Forward").valid());
    WZ_CHECK_EQ(registry.allocated_count(), static_cast<size_t>(1));

    // Final release frees the slot.
    registry.release(first);
    WZ_CHECK_FALSE(registry.find("Forward").valid());
    WZ_CHECK_EQ(registry.allocated_count(), static_cast<size_t>(0));
}

static void empty_name_is_rejected()
{
    TagRegistry<8> registry;
    WZ_CHECK_FALSE(registry.acquire("").valid());
}

static void full_registry_returns_null()
{
    TagRegistry<2> registry;
    WZ_CHECK(registry.acquire("a").valid());
    WZ_CHECK(registry.acquire("b").valid());
    WZ_CHECK_FALSE(registry.acquire("c").valid());  // full
}

static void released_slot_is_reused()
{
    TagRegistry<2> registry;
    const Tag a = registry.acquire("a");
    (void)registry.acquire("b");
    registry.release(a);
    const Tag c = registry.acquire("c");  // should reuse a's freed slot
    WZ_CHECK(c.valid());
    WZ_CHECK_EQ(registry.allocated_count(), static_cast<size_t>(2));
}

static void visit_enumerates_allocated_tags()
{
    TagRegistry<8> registry;
    (void)registry.acquire("Forward");
    (void)registry.acquire("Shadow");
    size_t seen = 0;
    registry.visit([&](std::string_view name, Tag tag) {
        WZ_CHECK(tag.valid());
        WZ_CHECK_FALSE(name.empty());
        ++seen;
    });
    WZ_CHECK_EQ(seen, static_cast<size_t>(2));
}

int main()
{
    WZ_RUN(default_tag_is_null);
    WZ_RUN(acquire_then_find_round_trips);
    WZ_RUN(missing_name_is_null_not_silent);
    WZ_RUN(distinct_names_get_distinct_tags);
    WZ_RUN(same_name_ref_counts_to_one_slot);
    WZ_RUN(empty_name_is_rejected);
    WZ_RUN(full_registry_returns_null);
    WZ_RUN(released_slot_is_reused);
    WZ_RUN(visit_enumerates_allocated_tags);
    WZ_TEST_RETURN();
}
