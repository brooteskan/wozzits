// tests/asset_inochi/puppet_residency_tests.cpp
//
// On-device coverage of publish_resident_puppet: a hand-built puppet becomes
// GPU-resident on the shared wozzits-rhi registry -- atlas pages as Texture2Ds
// and per-Part interleaved-vertex/index StructuredBuffers, each findable by the
// identities the returned ResidentPuppet reports. Also checks draw order, the
// per-Part colour modulation carry-through and the asset-owned tracker report.
// The device-backed test is skipped when no GPU device is available; the
// premultiply test below is pure and always runs.

#include <gtest/gtest.h>

#include <engine/assets/inochi/puppet_gpu.h>
#include <engine/rendering/engine_gpu_context.h>

#include <gpu/gpu.h>
#include <window/window2.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
    namespace ino = wz::engine::assets::inochi;

    ino::Node make_triangle_part(std::uint32_t uuid, float zsort, std::uint32_t atlas)
    {
        ino::Node n;
        n.uuid = uuid;
        n.kind = ino::NodeKind::Part;
        n.zsort = zsort;
        n.mesh.verts = { 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, 10.0f };
        n.mesh.uvs = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
        n.mesh.indices = { 0u, 1u, 2u };
        n.textures = { atlas };
        return n;
    }

    // A tiny decoded RGBA atlas page (opaque white). Opaque so the premultiply
    // at residency leaves it unchanged and the identity assertions still hold.
    ino::Texture make_atlas(std::uint32_t w, std::uint32_t h)
    {
        ino::Texture t;
        t.width = w;
        t.height = h;
        t.encoding = 0;  // decoded
        t.rgba.assign(static_cast<std::size_t>(w) * h * 4u, std::uint8_t{ 255 });
        return t;
    }
}

TEST(PuppetResidency, PublishesAtlasesAndPartBuffers)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "puppet_residency_test";
    window_desc.width = 64;
    window_desc.height = 64;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window for on-device puppet residency test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device for on-device puppet residency test";
    }

    {
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;

        // Two Parts sampling two atlas pages, under a translated root.
        ino::Puppet puppet;
        ino::Node root;
        root.kind = ino::NodeKind::Node;
        root.transform.trans = { 5.0f, 5.0f, 0.0f };
        root.children = { 1, 2 };
        puppet.nodes.push_back(root);
        ino::Node tinted = make_triangle_part(11, /*zsort*/ 1.0f, /*atlas*/ 0);
        tinted.tint = { 0.25f, 0.5f, 0.75f };
        tinted.screen_tint = { 0.1f, 0.2f, 0.3f };
        puppet.nodes.push_back(tinted);
        puppet.nodes.push_back(make_triangle_part(12, /*zsort*/ 2.0f, /*atlas*/ 1));
        puppet.textures.push_back(make_atlas(2, 2));
        puppet.textures.push_back(make_atlas(4, 4));

        wz::asset::AssetKey key{};
        key.content_hash.lo = 0xA1B2C3D4A1B2C3D4ull;
        key.content_hash.hi = 0x0102030405060708ull;

        int tracker_calls = 0;
        std::size_t tracked_identities = 0;
        wz::engine::assets::internal::RhiResourceTracker tracker =
            [&](const wz::asset::AssetKey&,
                std::vector<wz::rhi::ResourceIdentity> ids) {
                ++tracker_calls;
                tracked_identities += ids.size();
            };

        ino::ResidentPuppet resident;
        ASSERT_TRUE(ino::publish_resident_puppet(
            key, puppet, gpu.resources, tracker, logger, resident));

        // Two atlas pages + two Parts.
        ASSERT_EQ(resident.atlases.size(), 2u);
        ASSERT_EQ(resident.parts.size(), 2u);

        for (const wz::rhi::ResourceIdentity& atlas : resident.atlases) {
            EXPECT_TRUE(gpu.resources.find(atlas).valid())
                << "atlas page not resident";
        }
        for (const ino::ResidentPuppetPart& part : resident.parts) {
            EXPECT_TRUE(gpu.resources.find(part.vertices).valid())
                << "Part vertex buffer not resident";
            EXPECT_TRUE(gpu.resources.find(part.indices).valid())
                << "Part index buffer not resident";
            EXPECT_LT(part.atlas, resident.atlases.size());
            EXPECT_EQ(part.index_count, 3u);
            EXPECT_EQ(part.vertex_count, 3u);
        }

        // Draw order is DESCENDING zsort (Inochi draws higher zsort first / further
        // back): Part 12 (z=2) before Part 11 (z=1).
        EXPECT_GT(resident.parts[0].zsort, resident.parts[1].zsort);

        // Residency carries the per-Part colour modulation through to the record
        // the renderer packs into root constants (#276). Part 11 (the tinted one)
        // is second in draw order.
        EXPECT_EQ(resident.parts[1].tint, (std::array<float, 3>{ 0.25f, 0.5f, 0.75f }));
        EXPECT_EQ(
            resident.parts[1].screen_tint,
            (std::array<float, 3>{ 0.1f, 0.2f, 0.3f }));
        EXPECT_EQ(resident.parts[0].tint, (std::array<float, 3>{ 1.0f, 1.0f, 1.0f }));

        // The unmasked-Part fallback texture is resident and asset-owned (#275).
        EXPECT_TRUE(gpu.resources.find(resident.no_mask).valid())
            << "the 1x1 no-mask texture did not become resident";

        // The tracker got one asset-owned report of every published identity:
        // 2 atlas pages + the no-mask texture + 2 Parts * (vtx + idx) = 7.
        EXPECT_EQ(tracker_calls, 1);
        EXPECT_EQ(tracked_identities, 7u);
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}

// Pure (device-free): the premultiply applied to every atlas page at residency
// (#277). The fringing this fixes is a FILTERING artefact -- a bilinear tap
// straddling a Part border interpolates rgb and alpha independently -- so the
// premultiply has to happen before the sampler ever sees the page.
TEST(PuppetResidency, PremultipliesStraightAlphaAtlasTexels)
{
    const std::vector<std::uint8_t> straight = {
        255, 255, 255, 255,   // opaque white
        255,   0,   0, 128,   // half-transparent red
        255, 255, 255,   0,   // fully transparent, but WHITE rgb
          0,   0,   0,   0,   // fully transparent, black rgb (Inochi's usual)
         64, 128, 192,  51,   // arbitrary, 20% coverage
    };

    const std::vector<std::uint8_t> pm = ino::premultiply_rgba8(straight);
    ASSERT_EQ(pm.size(), straight.size());

    // Opaque texels survive bit-for-bit -- the puppet's interior must not shift.
    EXPECT_EQ(pm[0], 255u);
    EXPECT_EQ(pm[1], 255u);
    EXPECT_EQ(pm[2], 255u);

    // rgb scaled by coverage, rounded half up; alpha itself is never touched.
    EXPECT_EQ(pm[4], 128u);            // 255*128/255 = 128
    EXPECT_EQ(pm[5], 0u);
    EXPECT_EQ(pm[7], 128u);            // alpha preserved

    // The two transparent texels must both collapse to zero colour REGARDLESS of
    // their stored rgb. That is the whole fix: once a transparent texel carries
    // no colour, filtering across an edge can no longer drag the result toward
    // it, and a transparent WHITE texel can no longer wash the edge out either.
    EXPECT_EQ(pm[8], 0u);
    EXPECT_EQ(pm[9], 0u);
    EXPECT_EQ(pm[10], 0u);
    EXPECT_EQ(pm[11], 0u);
    EXPECT_EQ(pm[12], 0u);
    EXPECT_EQ(pm[15], 0u);

    EXPECT_EQ(pm[16], 13u);            // round(64*51/255)  = 12.8 -> 13
    EXPECT_EQ(pm[17], 26u);            // round(128*51/255) = 25.6 -> 26
    EXPECT_EQ(pm[18], 38u);            // round(192*51/255) = 38.4 -> 38
    EXPECT_EQ(pm[19], 51u);
}
