#include "wz_test.h"

#include <wozzits/rhi/gpu_resource_registry.h>

using namespace wz::rhi;

namespace
{
    // Counting fake backend — keeps the registry test free of any real GPU.
    struct FakeBackend final : GpuBackend
    {
        uint64_t next_id = 1;
        int creates = 0;
        int destroys = 0;
        int writes = 0;
        GpuResourceDesc last_desc{};

        BackendResource create(const GpuResourceDesc& desc) override
        {
            ++creates;
            last_desc = desc;
            return BackendResource{ next_id++ };
        }
        void destroy(BackendResource) override { ++destroys; }
        bool write(BackendResource, const void*, uint64_t, uint64_t) override
        {
            ++writes;
            return true;
        }
    };

    GpuResourceDesc persistent_buffer(uint64_t asset_id, Tag variant = {})
    {
        GpuResourceDesc desc;
        desc.identity = ResourceIdentity{ asset_id, variant };
        desc.size_bytes = 1024;
        desc.usage = ResourceUsage_Vertex;
        desc.cpu_access = ResourceCpuAccess::None;
        desc.residency = ResourceResidency::Persistent;
        return desc;
    }
}

static void acquire_creates_and_get_returns()
{
    FakeBackend backend;
    GpuResourceRegistry registry(backend);

    const GpuResourceHandle h = registry.acquire(persistent_buffer(0x1001));
    WZ_CHECK(h.valid());
    WZ_CHECK_EQ(backend.creates, 1);

    const GpuResource* res = registry.get(h);
    WZ_CHECK(res != nullptr);
    if (res) {
        WZ_CHECK(res->backend.valid());
        WZ_CHECK_EQ(res->desc.size_bytes, static_cast<uint64_t>(1024));
    }
    WZ_CHECK_EQ(registry.resident_count(), static_cast<size_t>(1));
}

// Same identity resolves to the existing resource — one backend create, not
// the duplicate uploads the old scattered caches could produce.
static void same_identity_dedups()
{
    FakeBackend backend;
    GpuResourceRegistry registry(backend);

    const GpuResourceHandle a = registry.acquire(persistent_buffer(0x2002));
    const GpuResourceHandle b = registry.acquire(persistent_buffer(0x2002));
    WZ_CHECK(a == b);
    WZ_CHECK_EQ(backend.creates, 1);
    WZ_CHECK_EQ(registry.resident_count(), static_cast<size_t>(1));
}

// Anonymous (asset_id == 0) resources are never deduplicated.
static void anonymous_always_creates()
{
    FakeBackend backend;
    GpuResourceRegistry registry(backend);

    const GpuResourceHandle a = registry.acquire(persistent_buffer(0));
    const GpuResourceHandle b = registry.acquire(persistent_buffer(0));
    WZ_CHECK_FALSE(a == b);
    WZ_CHECK_EQ(backend.creates, 2);
}

// Same asset, different realization variant -> distinct resources. This is the
// generalized `layout` discriminator (vertex-projected vs face-raw) working.
static void variant_distinguishes_identity()
{
    FakeBackend backend;
    GpuResourceRegistry registry(backend);
    ResourceVariantRegistry variants;
    const Tag vertex_projected = variants.acquire("vertex_projected");
    const Tag face_raw = variants.acquire("face_raw");

    const GpuResourceHandle a = registry.acquire(persistent_buffer(0x3003, vertex_projected));
    const GpuResourceHandle b = registry.acquire(persistent_buffer(0x3003, face_raw));
    WZ_CHECK_FALSE(a == b);
    WZ_CHECK_EQ(backend.creates, 2);

    // ...and re-acquiring one variant still dedups.
    const GpuResourceHandle a2 = registry.acquire(persistent_buffer(0x3003, vertex_projected));
    WZ_CHECK(a == a2);
    WZ_CHECK_EQ(backend.creates, 2);
}

// In-place update writes through for CPU-writable resources (the #145 path),
// and is a checkable failure for GPU-only ones — never a silent no-op.
static void update_respects_cpu_access()
{
    FakeBackend backend;
    GpuResourceRegistry registry(backend);

    GpuResourceDesc writable = persistent_buffer(0x4004);
    writable.cpu_access = ResourceCpuAccess::WriteFrequent;
    const GpuResourceHandle w = registry.acquire(writable);

    const int payload = 7;
    WZ_CHECK(registry.update(w, &payload, sizeof(payload)));
    WZ_CHECK_EQ(backend.writes, 1);

    // GPU-only resource: update must fail without touching the backend.
    const GpuResourceHandle gpu_only = registry.acquire(persistent_buffer(0x4005));
    WZ_CHECK_FALSE(registry.update(gpu_only, &payload, sizeof(payload)));
    WZ_CHECK_EQ(backend.writes, 1);
}

// Deferred release is PRECISE: a resource is destroyed exactly when the GPU has
// passed the timeline value that last used it, not after a fixed latency.
static void deferred_release_is_timeline_precise()
{
    FakeBackend backend;
    GpuResourceRegistry registry(backend);

    const GpuResourceHandle h = registry.acquire(persistent_buffer(0x5005));
    registry.touch(h, /*timeline*/ 10);
    registry.release(h);

    // GPU has only reached 5 — still in flight, must not be destroyed.
    registry.collect(/*completed*/ 5);
    WZ_CHECK_EQ(backend.destroys, 0);
    WZ_CHECK(registry.get(h) != nullptr);

    // GPU reaches 10 — now safe to reclaim.
    registry.collect(/*completed*/ 10);
    WZ_CHECK_EQ(backend.destroys, 1);
    WZ_CHECK(registry.get(h) == nullptr);   // stale after collection
}

// A released resource leaves identity lookup immediately, so re-acquiring the
// same identity builds a fresh resource rather than handing back a dying one.
static void released_identity_is_rebuilt_on_reacquire()
{
    FakeBackend backend;
    GpuResourceRegistry registry(backend);

    const GpuResourceHandle first = registry.acquire(persistent_buffer(0x6006));
    registry.release(first);
    WZ_CHECK_FALSE(registry.find(ResourceIdentity{ 0x6006, {} }).valid());

    const GpuResourceHandle second = registry.acquire(persistent_buffer(0x6006));
    WZ_CHECK_FALSE(first == second);
    WZ_CHECK_EQ(backend.creates, 2);
}

// Device loss invalidates EVERYTHING in one sweep: every backend resource
// destroyed, every handle stale, resident set empty, epoch advanced.
static void device_loss_is_one_sweep()
{
    FakeBackend backend;
    GpuResourceRegistry registry(backend);

    const GpuResourceHandle a = registry.acquire(persistent_buffer(0x7007));
    const GpuResourceHandle b = registry.acquire(persistent_buffer(0x7008));
    const GpuResourceHandle c = registry.acquire(persistent_buffer(0));  // anonymous
    WZ_CHECK_EQ(registry.resident_count(), static_cast<size_t>(3));

    const uint64_t epoch_before = registry.device_epoch();
    registry.on_device_lost();

    WZ_CHECK_EQ(backend.destroys, 3);                 // all, including anonymous
    WZ_CHECK_EQ(registry.resident_count(), static_cast<size_t>(0));
    WZ_CHECK(registry.get(a) == nullptr);             // every handle stale
    WZ_CHECK(registry.get(b) == nullptr);
    WZ_CHECK(registry.get(c) == nullptr);
    WZ_CHECK_EQ(registry.device_epoch(), epoch_before + 1);
}

// The desc carries its physical family + view-sufficient fields to the backend
// through the one registry door — a structured buffer keeps its stride, a 2D
// texture keeps its format/dims. (No per-semantic-kind create function.)
static void desc_carries_physical_family_to_backend()
{
    FakeBackend backend;
    GpuResourceRegistry registry(backend);

    registry.acquire(GpuResourceDesc::buffer(
        /*size*/ 2048, /*stride*/ 32, ResourceUsage_Sampled));
    WZ_CHECK(backend.last_desc.dimension == ResourceDimension::Buffer);
    WZ_CHECK_EQ(backend.last_desc.size_bytes, static_cast<uint64_t>(2048));
    WZ_CHECK_EQ(backend.last_desc.stride_bytes, 32u);

    registry.acquire(GpuResourceDesc::texture_2d(
        1920, 1080, TextureFormat::RGBA8Unorm, ResourceUsage_RenderTarget));
    WZ_CHECK(backend.last_desc.dimension == ResourceDimension::Texture2D);
    WZ_CHECK_EQ(backend.last_desc.width, 1920u);
    WZ_CHECK_EQ(backend.last_desc.height, 1080u);
    WZ_CHECK(backend.last_desc.format == TextureFormat::RGBA8Unorm);

    WZ_CHECK_EQ(backend.creates, 2);   // both anonymous -> both created
}

int main()
{
    WZ_RUN(acquire_creates_and_get_returns);
    WZ_RUN(desc_carries_physical_family_to_backend);
    WZ_RUN(same_identity_dedups);
    WZ_RUN(anonymous_always_creates);
    WZ_RUN(variant_distinguishes_identity);
    WZ_RUN(update_respects_cpu_access);
    WZ_RUN(deferred_release_is_timeline_precise);
    WZ_RUN(released_identity_is_rebuilt_on_reacquire);
    WZ_RUN(device_loss_is_one_sweep);
    WZ_TEST_RETURN();
}
