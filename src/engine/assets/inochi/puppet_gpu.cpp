// src/engine/assets/inochi/puppet_gpu.cpp
//
// Publish an Inochi2D puppet's GPU residency onto the shared wozzits-rhi
// registry: one resident Texture2D per decoded atlas page, and per drawable Part
// an interleaved (pos,uv) vertex StructuredBuffer + a u32 index StructuredBuffer,
// each under rhi_asset_identity(key, variant). The surrogate pattern -- the
// registry owns the GPU resources, the asset is their surrogate. Mirrors
// publish_resident_gaussian_splat_cloud / publish_resident_texture. See
// puppet_gpu.h.

#include <engine/assets/inochi/puppet_gpu.h>

#include <engine/assets/rhi_asset_identity.h>
#include <engine/rendering/rhi_mesh_bridge.h>  // acquire_pull_buffer

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wz::engine::assets::inochi
{
    static_assert(sizeof(PuppetVertex) == 16,
        "PuppetVertex must be 16 bytes (float2 pos + float2 uv) to match the "
        "puppet vertex-pull StructuredBuffer stride and the HLSL WzPuppetVertex");

    std::vector<std::uint8_t> premultiply_rgba8(
        const std::vector<std::uint8_t>& straight)
    {
        // Size is not required to be a multiple of 4; a ragged tail is copied as
        // zero rather than read past (callers pass width*height*4).
        std::vector<std::uint8_t> out(straight.size());
        for (std::size_t i = 0; i + 3 < straight.size(); i += 4) {
            const std::uint32_t a = straight[i + 3];
            // Exact round-half-up 8-bit premultiply. a=255 is preserved
            // bit-for-bit and a=0 collapses to 0, which is the whole point of
            // the exercise. Runs once per page when the puppet loads, never per
            // frame -- a 2048x2048 page is ~12M of these.
            for (std::size_t c = 0; c < 3; ++c) {
                out[i + c] = static_cast<std::uint8_t>(
                    (straight[i + c] * a + 127u) / 255u);
            }
            out[i + 3] = straight[i + 3];
        }
        return out;
    }

    bool publish_resident_puppet(
        const wz::asset::AssetKey& key,
        const Puppet& puppet,
        wz::rhi::GpuResourceRegistry& gpu_resources,
        const internal::RhiResourceTracker& rhi_resource_tracker,
        wz::Logger& logger,
        ResidentPuppet& out)
    {
        out = ResidentPuppet{};

        const PuppetDrawList list = build_puppet_draw_list(puppet);
        if (list.empty()) {
            logger.warn("puppet has no drawable parts; nothing to make resident");
            return false;
        }

        // Everything acquired so far, released together if a later step fails
        // (nothing is handed to the tracker until the whole publish succeeds).
        std::vector<wz::rhi::GpuResourceHandle> acquired;
        std::vector<wz::rhi::ResourceIdentity> identities;

        const auto fail = [&](const std::string& message) {
            for (const wz::rhi::GpuResourceHandle& handle : acquired) {
                if (handle.valid()) {
                    gpu_resources.release(handle);
                }
            }
            logger.warn(message);
            out = ResidentPuppet{};
            return false;
        };

        // 1) Atlas pages -> resident Texture2D. A page that is not decoded (e.g.
        // still-encoded BC7) is left invalid; Parts sampling it are dropped below.
        out.atlases.assign(puppet.textures.size(), wz::rhi::ResourceIdentity{});
        std::vector<std::uint8_t> atlas_ok(puppet.textures.size(), 0u);
        for (std::size_t ti = 0; ti < puppet.textures.size(); ++ti) {
            const Texture& tex = puppet.textures[ti];
            if (tex.rgba.empty() || tex.width == 0 || tex.height == 0) {
                logger.warn(
                    "puppet atlas page " + std::to_string(ti)
                    + " is not decoded RGBA; skipped");
                continue;
            }

            // Premultiply the page before upload (#277). Inochi's .inp textures
            // are STRAIGHT alpha, and their fully-transparent texels are stored
            // black. A bilinear tap that straddles a Part's border interpolates
            // rgb and alpha independently, so the filtered rgb is dragged toward
            // that black while the filtered alpha still says "partly visible" --
            // the dark fringe. Premultiplying FIRST makes the interpolation
            // correct, because a transparent texel then contributes zero colour
            // instead of black colour. (Premultiplying in the pixel shader
            // instead cannot fix this: it happens after the sampler has already
            // filtered, and rgb*a with a ONE/INV_SRC_ALPHA blend is algebraically
            // the same result straight alpha + SRC_ALPHA/INV_SRC_ALPHA gives.)
            std::vector<std::uint8_t> premultiplied = premultiply_rgba8(tex.rgba);

            wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::texture_2d(
                tex.width,
                tex.height,
                wz::rhi::TextureFormat::RGBA8Unorm,
                wz::rhi::ResourceUsage_Sampled);
            desc.cpu_access = wz::rhi::ResourceCpuAccess::WriteOnce;
            desc.identity = wz::rhi::ResourceIdentity{
                rhi_asset_identity(key, "atlas_" + std::to_string(ti)), {} };

            const wz::rhi::GpuResourceHandle handle = gpu_resources.acquire(desc);
            const bool uploaded =
                handle.valid()
                && gpu_resources.update_mip(
                    handle, /*mip_level*/ 0, premultiplied.data(),
                    static_cast<std::uint64_t>(premultiplied.size()));
            if (!uploaded) {
                if (handle.valid()) {
                    gpu_resources.release(handle);
                }
                return fail("puppet atlas residency upload failed");
            }

            acquired.push_back(handle);
            identities.push_back(desc.identity);
            out.atlases[ti] = desc.identity;
            atlas_ok[ti] = 1u;
        }

        // 1b) The 1x1 opaque-white "no mask" texture every unmasked Part binds
        // (#275). Published per puppet so it is released with the puppet's other
        // resources by the same tracker report.
        {
            const std::array<std::uint8_t, 4> white{ 255u, 255u, 255u, 255u };
            wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::texture_2d(
                1u, 1u,
                wz::rhi::TextureFormat::RGBA8Unorm,
                wz::rhi::ResourceUsage_Sampled);
            desc.cpu_access = wz::rhi::ResourceCpuAccess::WriteOnce;
            desc.identity = wz::rhi::ResourceIdentity{
                rhi_asset_identity(key, "no_mask"), {} };

            const wz::rhi::GpuResourceHandle handle = gpu_resources.acquire(desc);
            const bool uploaded =
                handle.valid()
                && gpu_resources.update_mip(
                    handle, /*mip_level*/ 0, white.data(),
                    static_cast<std::uint64_t>(white.size()));
            if (!uploaded) {
                if (handle.valid()) {
                    gpu_resources.release(handle);
                }
                return fail("puppet no-mask texture residency failed");
            }
            acquired.push_back(handle);
            identities.push_back(desc.identity);
            out.no_mask = desc.identity;
        }

        // 2) ONE shared interleaved-vertex buffer + ONE shared index buffer for
        // the whole puppet (#278), every drawable Part's geometry concatenated in
        // draw order. Each Part records where its slice starts; its indices stay
        // Part-local and the vertex shader adds the base.
        //
        // The pair used to be per Part. Two costs made that untenable once the
        // deform seam landed: GpuResourceRegistry::update() is a synchronous GPU
        // flush, so a deforming puppet stalled the frame once per moving Part;
        // and per-Part pull buffers gave every Part its own slot-2 descriptor
        // table. One buffer means one flush per deform and shared tables.
        std::vector<PuppetVertex> all_vertices;
        std::vector<std::uint32_t> all_indices;
        out.parts.reserve(list.parts.size());
        for (const PuppetPartDraw& part : list.parts) {
            if (part.atlas_texture >= atlas_ok.size()
                || !atlas_ok[part.atlas_texture])
            {
                logger.warn(
                    "puppet Part (node " + std::to_string(part.node_index)
                    + ") atlas page not resident; Part dropped");
                continue;
            }

            ResidentPuppetPart rp;
            rp.vertex_base = static_cast<std::uint32_t>(all_vertices.size());
            rp.index_base = static_cast<std::uint32_t>(all_indices.size());
            rp.index_count = static_cast<std::uint32_t>(part.indices.size());
            rp.vertex_count = static_cast<std::uint32_t>(part.vertices.size());
            rp.atlas = part.atlas_texture;
            rp.node_index = part.node_index;
            rp.placement = part.placement;
            rp.opacity = part.opacity;
            rp.tint = part.tint;
            rp.screen_tint = part.screen_tint;
            rp.mask_source = part.mask_source;
            rp.mask_threshold = part.mask_threshold;
            rp.mask_inverted = part.mask_inverted;
            rp.blend = part.blend;
            rp.zsort = part.zsort;
            out.parts.push_back(rp);

            all_vertices.insert(
                all_vertices.end(), part.vertices.begin(), part.vertices.end());
            all_indices.insert(
                all_indices.end(), part.indices.begin(), part.indices.end());
        }

        if (out.parts.empty() || all_vertices.empty() || all_indices.empty()) {
            return fail("puppet has no Part with resident geometry");
        }
        out.vertex_count = static_cast<std::uint32_t>(all_vertices.size());
        out.index_count = static_cast<std::uint32_t>(all_indices.size());

        {
            // WriteFrequent: the deform seam (S3) re-uploads the vertices each
            // frame any Part's mesh moves. Indices stay WriteOnce -- they are
            // Part-local and deform never renumbers them.
            const std::uint64_t vtx_asset_id = rhi_asset_identity(key, "vtx");
            out.vertices = wz::rhi::ResourceIdentity{ vtx_asset_id, {} };
            const wz::rhi::GpuResourceHandle vhandle =
                wz::engine::rendering::acquire_pull_buffer(
                    gpu_resources, vtx_asset_id, wz::rhi::Tag{},
                    all_vertices.data(),
                    static_cast<std::uint64_t>(all_vertices.size())
                        * sizeof(PuppetVertex),
                    static_cast<std::uint32_t>(sizeof(PuppetVertex)),
                    wz::rhi::ResourceCpuAccess::WriteFrequent);
            if (!vhandle.valid()) {
                return fail("puppet shared vertex buffer residency failed");
            }
            acquired.push_back(vhandle);
            identities.push_back(out.vertices);

            const std::uint64_t idx_asset_id = rhi_asset_identity(key, "idx");
            out.indices = wz::rhi::ResourceIdentity{ idx_asset_id, {} };
            const wz::rhi::GpuResourceHandle ihandle =
                wz::engine::rendering::acquire_pull_buffer(
                    gpu_resources, idx_asset_id, wz::rhi::Tag{},
                    all_indices.data(),
                    static_cast<std::uint64_t>(all_indices.size())
                        * sizeof(std::uint32_t),
                    static_cast<std::uint32_t>(sizeof(std::uint32_t)));
            if (!ihandle.valid()) {
                return fail("puppet shared index buffer residency failed");
            }
            acquired.push_back(ihandle);
            identities.push_back(out.indices);
        }

        if (out.parts.empty()) {
            return fail("puppet has no resident Parts (atlas pages missing?)");
        }

        out.bounds_min = list.bounds_min;
        out.bounds_max = list.bounds_max;

        if (rhi_resource_tracker) {
            rhi_resource_tracker(key, std::move(identities));
        }
        return true;
    }
}
