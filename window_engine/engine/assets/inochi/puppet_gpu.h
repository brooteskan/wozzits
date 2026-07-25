#pragma once

// engine/assets/inochi/puppet_gpu.h
//
// GPU residency for an Inochi2D puppet, on the shared wozzits-rhi registry (the
// surrogate pattern -- the registry owns the GPU resources, the asset is their
// surrogate). Publishes each decoded atlas page as a resident Texture2D and each
// drawable Part's interleaved (pos,uv) vertices + u32 indices as resident
// StructuredBuffers, every resource under rhi_asset_identity(key, variant). The
// returned ResidentPuppet carries the draw-ordered per-Part records (identities +
// placement + blend + counts) that the puppet renderer binds and records.
// Mirrors publish_resident_gaussian_splat_cloud / publish_resident_texture. See
// the inochi-runtime-track (S2).

#include <engine/assets/inochi/inochi_puppet.h>
#include <engine/assets/inochi/puppet_draw_list.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>  // RhiResourceTracker

#include <asset/types.h>       // wz::asset::AssetKey
#include <logging/logger.h>
#include <wozzits/rhi/gpu_resource.h>  // ResourceIdentity, GpuResourceHandle

#include <array>
#include <cstdint>
#include <vector>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::assets::inochi
{
    // One resident Part: the identities of its resident interleaved-vertex and
    // index StructuredBuffers, the atlas page it samples (index into
    // ResidentPuppet::atlases), the draw counts, and the static placement /
    // opacity / blend / zsort carried from the draw list.
    struct ResidentPuppetPart
    {
        wz::rhi::ResourceIdentity vertices;
        wz::rhi::ResourceIdentity indices;
        std::uint32_t index_count = 0;
        std::uint32_t vertex_count = 0;
        std::uint32_t atlas = 0;             // index into ResidentPuppet::atlases
        std::size_t node_index = 0;          // index into Puppet::nodes (deform match)
        Affine2D placement;                  // Part-local pixels -> puppet pixels
        float opacity = 1.0f;
        std::array<float, 3> tint{ 1.0f, 1.0f, 1.0f };        // multiply (#276)
        std::array<float, 3> screen_tint{ 0.0f, 0.0f, 0.0f };  // screen (#276)
        BlendMode blend = BlendMode::Normal;
        float zsort = 0.0f;
    };

    // The resident form of a puppet: the atlas-page identities (one per
    // Puppet::textures; an invalid entry = a page that could not be made
    // resident, e.g. still-encoded BC7), the draw-ordered resident Parts, and
    // puppet-space bounds. The GPU buffers/textures live in the registry under
    // these identities; this metadata is what the renderer reads.
    struct ResidentPuppet
    {
        std::vector<wz::rhi::ResourceIdentity> atlases;  // per Puppet::textures
        std::vector<ResidentPuppetPart> parts;           // draw order (back -> front)
        std::array<float, 2> bounds_min{ 0.0f, 0.0f };
        std::array<float, 2> bounds_max{ 0.0f, 0.0f };

        [[nodiscard]] bool empty() const noexcept { return parts.empty(); }
    };

    // Convert a straight-alpha RGBA8 buffer to PREMULTIPLIED alpha in place of a
    // copy (rgb *= a, alpha untouched), rounding half up.
    //
    // Part of the puppet's residency contract (#277): atlas pages are made
    // resident premultiplied, and puppet_ps.hlsl reads them as such. Inochi
    // stores its textures straight-alpha with black transparent texels, so a
    // bilinear tap across a Part's border would otherwise drag the filtered rgb
    // toward black while the filtered alpha still reads "partly visible" -- the
    // dark edge fringe. Premultiplying BEFORE the sampler filters is the fix;
    // doing it in the pixel shader afterwards is algebraically a no-op.
    [[nodiscard]] std::vector<std::uint8_t> premultiply_rgba8(
        const std::vector<std::uint8_t>& straight);

    // Publish `puppet`'s residency onto `gpu_resources` under the asset `key`: one
    // Texture2D per decoded atlas page, and per drawable Part (draw order =
    // build_puppet_draw_list) an interleaved vertex StructuredBuffer + a u32 index
    // StructuredBuffer. Fills `out` with the resulting identities + draw metadata.
    // Every published identity is reported to `rhi_resource_tracker` for
    // asset-owned release. Best-effort per the surrogate pattern: on any acquire/
    // upload failure the function releases what it made, logs, and returns false
    // (the puppet stays unrealizable until residency succeeds). Parts whose atlas
    // page is not resident are dropped (logged), not failed.
    [[nodiscard]] bool publish_resident_puppet(
        const wz::asset::AssetKey& key,
        const Puppet& puppet,
        wz::rhi::GpuResourceRegistry& gpu_resources,
        const internal::RhiResourceTracker& rhi_resource_tracker,
        wz::Logger& logger,
        ResidentPuppet& out);
}
