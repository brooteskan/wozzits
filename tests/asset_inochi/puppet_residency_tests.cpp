// tests/asset_inochi/puppet_residency_tests.cpp
//
// On-device coverage of publish_resident_puppet: a hand-built puppet becomes
// GPU-resident on the shared wozzits-rhi registry -- atlas pages as Texture2Ds
// and per-Part interleaved-vertex/index StructuredBuffers, each findable by the
// identities the returned ResidentPuppet reports. Also checks draw order and the
// asset-owned tracker report. Skipped when no GPU device is available.

#include <gtest/gtest.h>

#include <engine/assets/inochi/puppet_gpu.h>
#include <engine/rendering/engine_gpu_context.h>

#include <gpu/gpu.h>
#include <window/window2.h>
#include <wozzits/rhi/gpu_resource_registry.h>

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

    // A tiny decoded RGBA atlas page (opaque white).
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
        puppet.nodes.push_back(make_triangle_part(11, /*zsort*/ 1.0f, /*atlas*/ 0));
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

        // The tracker got one asset-owned report of every published identity:
        // 2 atlas pages + 2 Parts * (vtx + idx) = 6.
        EXPECT_EQ(tracker_calls, 1);
        EXPECT_EQ(tracked_identities, 6u);
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
