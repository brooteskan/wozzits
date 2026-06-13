#include "wz_test.h"

#include <wozzits/rhi/handle.h>

#include <string>

using wz::rhi::SlotMap;

struct FakeResource {};

static void default_handle_is_invalid()
{
    SlotMap<int>::Handle handle;
    WZ_CHECK_FALSE(handle.valid());
}

static void insert_then_get_round_trips()
{
    SlotMap<std::string> map;
    const auto handle = map.insert("mesh_buffer");
    WZ_CHECK(handle.valid());
    const std::string* value = map.get(handle);
    WZ_CHECK(value != nullptr);
    if (value) {
        WZ_CHECK_EQ(*value, std::string{ "mesh_buffer" });
    }
    WZ_CHECK_EQ(map.size(), static_cast<size_t>(1));
}

// The whole point of generational handles: a handle to an erased slot becomes
// stale and resolves to nullptr instead of dereferencing freed memory. This is
// the device-residency analogue of the silent use-after-free we want gone.
static void stale_handle_resolves_to_nullptr()
{
    SlotMap<int> map;
    const auto handle = map.insert(42);
    WZ_CHECK(map.get(handle) != nullptr);

    WZ_CHECK(map.erase(handle));
    WZ_CHECK(map.get(handle) == nullptr);   // stale: erased
    WZ_CHECK_FALSE(map.erase(handle));      // double-erase is a no-op, not UB
}

// Reusing a freed slot bumps its generation, so an old handle to that index
// does not silently alias the new occupant.
static void reused_slot_invalidates_old_handle()
{
    SlotMap<int> map;
    const auto first = map.insert(1);
    map.erase(first);
    const auto second = map.insert(2);  // reuses the freed index

    WZ_CHECK(map.get(second) != nullptr);
    WZ_CHECK(map.get(first) == nullptr);   // old handle stays stale
    WZ_CHECK_FALSE(first == second);        // same index, different generation
}

int main()
{
    WZ_RUN(default_handle_is_invalid);
    WZ_RUN(insert_then_get_round_trips);
    WZ_RUN(stale_handle_resolves_to_nullptr);
    WZ_RUN(reused_slot_invalidates_old_handle);
    WZ_TEST_RETURN();
}
