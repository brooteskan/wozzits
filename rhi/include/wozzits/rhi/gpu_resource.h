#pragma once

// wozzits/rhi/gpu_resource.h
//
// The data contract for a GPU resource: what you declare to create one, and
// the backend-agnostic interface the registry delegates real GPU work to.
//
// This is the translation of the "seven lifecycles -> four parameters"
// analysis into types. The parameters that actually vary across the old
// renderer's scattered caches are made explicit here (identity, residency,
// cpu_access, usage) instead of being re-implemented per cache.

#include <wozzits/rhi/handle.h>
#include <wozzits/rhi/tag_registry.h>

#include <cstdint>

namespace wz::rhi
{
    // A resource's "realization variant" is an OPEN set: a mesh's vertex
    // buffer, a field's vertex-projected vs face-raw buffer, future forms. Like
    // every open identity in this repo it is a registered Tag, not an enum.
    // (This is the generalization of the `layout` discriminator that got bolted
    // onto resident field buffers in the old engine.)
    inline constexpr size_t kMaxResourceVariants = 256;
    using ResourceVariantRegistry = TagRegistry<kMaxResourceVariants>;

    // Identity of a GPU resource as a projection of an asset into device space.
    // asset_id is an opaque content id supplied by the asset bridge; 0 means
    // "anonymous" — a transient/scratch resource that is never deduplicated.
    struct ResourceIdentity
    {
        uint64_t asset_id = 0;
        Tag      variant{};

        [[nodiscard]] bool anonymous() const noexcept { return asset_id == 0; }

        friend bool operator==(const ResourceIdentity&,
                              const ResourceIdentity&) = default;
    };

    // How long a resource lives. Drives ownership and aliasing eligibility.
    enum class ResourceResidency : uint8_t { Persistent, Transient, OneShot };

    // CPU access intent. Drives memory placement; the mutable cases are the
    // per-frame refresh path (#145).
    enum class ResourceCpuAccess : uint8_t { None, WriteOnce, WriteFrequent, Readback };

    // How the resource is used. Closed bitmask (drives barriers + heap /
    // descriptor choice in a backend).
    enum ResourceUsageBits : uint32_t
    {
        ResourceUsage_None         = 0,
        ResourceUsage_Sampled      = 1u << 0,   // SRV
        ResourceUsage_Storage      = 1u << 1,   // UAV
        ResourceUsage_Vertex       = 1u << 2,
        ResourceUsage_Index        = 1u << 3,
        ResourceUsage_Uniform      = 1u << 4,
        ResourceUsage_RenderTarget = 1u << 5,
        ResourceUsage_DepthStencil = 1u << 6,
        ResourceUsage_CopySrc      = 1u << 7,
        ResourceUsage_CopyDst      = 1u << 8,
    };

    struct GpuResourceDesc
    {
        ResourceIdentity  identity{};
        uint64_t          size_bytes = 0;
        uint32_t          usage      = ResourceUsage_None;
        ResourceCpuAccess cpu_access = ResourceCpuAccess::None;
        ResourceResidency residency  = ResourceResidency::Persistent;
    };

    // Opaque backend resource id. The backend assigns it; the registry stores
    // it and never interprets it. (id == 0 is the invalid sentinel.)
    struct BackendResource
    {
        uint64_t id = 0;

        [[nodiscard]] bool valid() const noexcept { return id != 0; }

        friend bool operator==(BackendResource, BackendResource) = default;
    };

    // The interface the registry delegates real GPU work to. Device-agnostic:
    // a DX12/Vulkan/Metal backend implements this; tests implement a fake. The
    // registry (identity + lifetime policy) thus stays free of any concrete API
    // and free of any device dependency.
    class GpuBackend
    {
    public:
        virtual ~GpuBackend() = default;

        virtual BackendResource create(const GpuResourceDesc& desc) = 0;
        virtual void destroy(BackendResource resource) = 0;

        // Write CPU data into a CPU-writable resource. The registry only calls
        // this for resources whose cpu_access permits it.
        virtual bool write(BackendResource resource,
                          const void* data,
                          uint64_t size,
                          uint64_t offset) = 0;
    };

    // The registry's public view of a resident resource.
    struct GpuResource
    {
        GpuResourceDesc desc{};
        BackendResource backend{};
    };

    struct GpuResourceTag {};
    using GpuResourceHandle = GenerationalHandle<GpuResourceTag>;
}
