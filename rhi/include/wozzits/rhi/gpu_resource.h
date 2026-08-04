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

    // Physical resource family. Closed, fixed set -> enum (these are the kinds
    // a backend allocates differently, and they do not grow). Semantic role —
    // "mesh", "field", "mask" — is deliberately NOT here; that is usage plus a
    // realization-variant Tag. This is "typed by physical family", not "typed
    // by semantic role" (which would be the BuiltinRenderProgram disease again).
    enum class ResourceDimension : uint8_t { Buffer, Texture2D, Texture3D };

    // Texture formats — closed bounded set, enum (exhaustiveness-checked, never
    // a registry). Minimal v0 set; extend as a bounded enum.
    enum class TextureFormat : uint8_t
    {
        Undefined,
        RGBA8Unorm,
        RGBA16Float,
        RGBA32Float,
        R32Float,
        D32Float,
        D24UnormS8Uint,
        // Display-referred 8-bit colour: identical bytes to RGBA8Unorm, but the
        // sampler decodes sRGB -> linear on read (and so filters in linear).
        // Appended rather than grouped next to RGBA8Unorm so every prior ordinal
        // stays stable. Honours the authored TextureColorSpace (#324).
        RGBA8UnormSrgb,
        // Keep last. Count equals the member total, so appending a format trips
        // the static_assert beside every exhaustive mapping (rhi -> gpu -> DXGI)
        // rather than silently falling through. Pinning a named member's ordinal
        // would be append-blind (cf. C3-H5 on #316); Count is not. #324.
        Count,
    };

    struct GpuResourceDesc
    {
        ResourceIdentity  identity{};
        ResourceDimension dimension  = ResourceDimension::Buffer;
        uint32_t          usage      = ResourceUsage_None;
        ResourceCpuAccess cpu_access = ResourceCpuAccess::None;
        ResourceResidency residency  = ResourceResidency::Persistent;

        // Buffer family (dimension == Buffer). stride_bytes 0 == raw /
        // byte-address; non-zero == structured (the stride the SRV needs).
        uint64_t size_bytes   = 0;
        uint32_t stride_bytes = 0;

        // Texture family (dimension == Texture2D / Texture3D). format + dims are
        // what the view needs; depth applies to Texture3D.
        uint32_t      width      = 0;
        uint32_t      height     = 0;
        uint32_t      depth      = 1;
        uint32_t      mip_levels = 1;
        TextureFormat format     = TextureFormat::Undefined;

        // Named constructors keep the two families consistent at call sites
        // while preserving the single registry door (acquire(GpuResourceDesc)).
        [[nodiscard]] static GpuResourceDesc buffer(
            uint64_t size,
            uint32_t stride = 0,
            uint32_t usage_bits = ResourceUsage_None,
            ResourceCpuAccess cpu = ResourceCpuAccess::None)
        {
            GpuResourceDesc d;
            d.dimension = ResourceDimension::Buffer;
            d.size_bytes = size;
            d.stride_bytes = stride;
            d.usage = usage_bits;
            d.cpu_access = cpu;
            return d;
        }

        [[nodiscard]] static GpuResourceDesc texture_2d(
            uint32_t w,
            uint32_t h,
            TextureFormat fmt,
            uint32_t usage_bits = ResourceUsage_None)
        {
            GpuResourceDesc d;
            d.dimension = ResourceDimension::Texture2D;
            d.width = w;
            d.height = h;
            d.format = fmt;
            d.usage = usage_bits;
            return d;
        }

        // Structural equality over every field (all members are trivially
        // comparable; ResourceIdentity has its own defaulted ==). The frame
        // graph uses this to decide alias-group compatibility: two transients
        // may share backing memory only if their descs match exactly.
        friend bool operator==(const GpuResourceDesc&, const GpuResourceDesc&) = default;
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

        // Write CPU data into one mip level of a texture resource. `mip_level`
        // is 0-based (0 == the full-resolution surface). `data` is tightly
        // packed for that mip's dimensions — width>>mip and height>>mip, each
        // clamped to a minimum of 1 — with no row padding; the backend owns any
        // device row-pitch alignment for the upload. `size` is the exact
        // tightly-packed byte count (mip_width * mip_height * mip_depth *
        // texel_bytes). Returns false for a stale resource, a non-texture
        // resource, an out-of-range mip level, or a size that does not match the
        // level's tightly-packed footprint.
        //
        // Non-pure with a failing default so existing backends and test fakes
        // stay source-compatible; a backend that supports mip chains overrides
        // it. The registry mediates it via GpuResourceRegistry::update_mip.
        virtual bool write_texture_mip(BackendResource /*resource*/,
                                       uint32_t /*mip_level*/,
                                       const void* /*data*/,
                                       uint64_t /*size*/)
        {
            return false;
        }
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
