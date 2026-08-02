// tests/render/rhi_puppet_render_tests.cpp
//
// On-device integration coverage for the Inochi2D puppet render path (inochi
// S2b). It builds, end to end:
//   * a Puppet asset from the real Aka.inp fixture (loads + publishes residency:
//     atlas Texture2Ds + per-Part interleaved-vertex/index StructuredBuffers),
//   * a puppet render program (MeshVertexPull; a Screen view head + the
//     PuppetVertices/PuppetIndices/PuppetAtlas object SRG + a clamp sampler),
//   * a kPuppetRhiRenderableSchema renderable binding them,
// then drives one device frame through RhiSceneRenderer and asserts the puppet
// realizes (one DrawPacket per Part) and records WITHOUT the recorder rejecting
// the draws. This structurally proves the wiring; it does NOT verify the puppet
// looks right -- there is no view of the viewport. Skipped when no Aka.inp
// fixture or no GPU device is available.
//
// The shaders + SRG come from ensure_puppet_program(), i.e. the SHIPPING puppet
// program, staged into a temp resource root. The test used to carry its own
// byte-compatible copies of both; that made it possible to change the shipping
// shader and the SRG together and still watch the test pass against its own
// stale pair. Driving the real program is what makes the root-constant block
// size, the shader source and the PSO blend mode fail together when they drift.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/puppet_asset_module.h>
#include <engine/assets/puppet_program.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/engine_gpu_context.h>
#include <engine/rendering/rhi_scene_renderer.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <gpu/texture.h>
#include <gpu/dx12/dx12_internal.h>
#include <math/mat4.h>
#include <math/math_types.h>
#include <window/window2.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    namespace ea = wz::engine::assets;
    namespace fs = std::filesystem;
}

TEST(RhiPuppetRender, RealizesAndRecordsPartPackets)
{
    const fs::path fixture =
        fs::path(WZ_TEST_FIXTURE_DIR) / "inochi" / "Aka.inp";
    if (!fs::exists(fixture)) {
        GTEST_SKIP() << "no Aka.inp fixture for the on-device puppet render test";
    }

    wz::window::WindowDesc window_desc{};
    window_desc.title = "rhi_puppet_render_test";
    window_desc.width = 256;
    window_desc.height = 256;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for on-device puppet render test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for on-device puppet render test";
    }

    {
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;

        const fs::path root =
            fs::temp_directory_path()
            / ("wozzits_puppet_render_test_"
               + std::to_string(static_cast<unsigned long long>(
                     std::chrono::steady_clock::now()
                         .time_since_epoch()
                         .count())));
        fs::remove_all(root);

        ea::EngineAssetLibrary assets(gpu, logger, root.string());

        // 1) The .inp as a raw-file carrier (absolute fixture path).
        const wz::asset::AssetKey inp_file = assets.files().register_file_node(
            wz::fs::join(WZ_TEST_FIXTURE_DIR, "inochi/Aka.inp"),
            ea::kRawFileSchema,
            ea::kAssetTypeRawFile);
        ASSERT_FALSE(inp_file == wz::asset::AssetKey{});

        // 2) The Puppet asset (loads the container + publishes residency).
        const ea::PuppetAsset puppet =
            assets.puppets().create_puppet_from_file(
                ea::PuppetFromFileDesc{
                    .name = "puppet/aka",
                    .source_file = inp_file });
        ASSERT_TRUE(puppet.valid());

        // 3) The SHIPPING puppet render program: stages the canonical shaders
        // into this temp root, registers the pair (vs_5_1/ps_5_1 -- the space2
        // bindings require SM 5.1) and builds the fixed puppet SRG.
        const ea::RenderProgramAsset program = ea::ensure_puppet_program(
            logger,
            assets.files(),
            assets.shaders(),
            assets.render_programs());
        ASSERT_TRUE(program.valid());

        // 4) The puppet renderable binding the two.
        const ea::RenderableAsset renderable =
            assets.renderables().create_puppet_rhi(
                ea::PuppetRhiRenderableDesc{
                    .name = "puppet/renderable",
                    .puppet = puppet,
                    .program = program });
        ASSERT_TRUE(renderable.valid());

        ASSERT_TRUE(assets.commit());
        const ea::ResolveReport resolve = assets.resolve_all();
        for (const ea::ResolveFailure& f : resolve.failures) {
            ADD_FAILURE() << "resolve failure: error="
                          << static_cast<int>(f.error);
        }
        ASSERT_TRUE(resolve.ok());

        // #300: the TYPED provisioning path must see its own blend variants.
        // ensure_puppet_program registers them through create_custom, i.e. under
        // a different schema than the graph path uses -- a lookup keyed on
        // schema found none of them and every Part silently drew Normal, which
        // no "did it render" assertion can distinguish from working.
        {
            const ea::PuppetProgramVariants variants =
                ea::puppet_program_variants(
                    assets.system(), assets.render_programs(), program.key);
            std::vector<wz::asset::AssetKey> seen;
            for (std::size_t i = 0; i < ea::kPuppetProgramBlendCount; ++i) {
                const auto blend = static_cast<ea::PuppetProgramBlend>(i);
                const wz::asset::AssetKey& key = variants.key_for(blend);
                EXPECT_FALSE(key == wz::asset::AssetKey{})
                    << "variant " << i << " has no program";
                for (const wz::asset::AssetKey& other : seen) {
                    EXPECT_FALSE(other == key)
                        << "variant " << i << " collapsed onto another variant "
                           "-- the typed path's programs were not found (#300)";
                }
                seen.push_back(key);

                // ...and the one found for a blend actually COMPILED to that
                // blend, so the PSO a Part draws through cannot disagree with
                // the variant it was bucketed under.
                const auto* compiled = assets.system().find_compiled(key);
                ASSERT_NE(compiled, nullptr);
                const ea::RenderProgramData* data =
                    assets.render_programs().get_render_program_data(
                        compiled->handle);
                ASSERT_NE(data, nullptr);
                EXPECT_EQ(data->blend_mode, ea::rhi_blend_for(blend))
                    << "variant " << i << " compiled to the wrong blend state";
            }
        }

        // The puppet became resident: its first atlas page (Aka.inp atlases the
        // per-part textures into a few pages).
        const wz::rhi::GpuResourceHandle atlas0 = gpu.resources.find(
            wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(puppet.output, "atlas_0"), {} });
        ASSERT_TRUE(atlas0.valid())
            << "puppet atlas page 0 did not become resident";

        // Drive one device frame through the renderer.
        wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);

        ea::SceneNodeAsset node{};
        node.id = wz::scene::AuthoredEntityId{ 1 };
        node.name = "puppet";
        node.visible = true;
        node.renderable_asset = renderable.output;
        const std::vector<ea::SceneNodeAsset> nodes{ node };

        const wz::math::Mat4 view_projection = wz::math::Mat4::identity();
        const wz::math::Vec3 camera_world_pos{ 0.0f, 0.0f, 0.0f };

        ASSERT_TRUE(wz::gpu::begin_frame(device));
        wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
        const bool recorded = renderer.render_scene(
            nodes, assets, view_projection, camera_world_pos);
        EXPECT_TRUE(recorded)
            << "puppet failed to realize or the recorder rejected the Part draws";
        ASSERT_TRUE(wz::gpu::end_frame(device));

        // The puppet must render VISIBLE pixels to the backbuffer (this structural
        // test historically only checked recording, never output). Read it back and
        // count texels that differ from the ~(26,26,31) clear.
        {
            std::vector<std::uint8_t> bb;
            ASSERT_TRUE(wz::gpu::dx12::internal::read_backbuffer_rgba8_dx12(
                device, bb));
            std::size_t nonclear = 0;
            for (std::size_t i = 0; i + 3 < bb.size(); i += 4) {
                const int dr = static_cast<int>(bb[i]) - 26;
                const int dg = static_cast<int>(bb[i + 1]) - 26;
                const int db = static_cast<int>(bb[i + 2]) - 31;
                if (dr > 15 || dr < -15 || dg > 15 || dg < -15
                    || db > 15 || db < -15) {
                    ++nonclear;
                }
            }
            EXPECT_GT(nonclear, 0u)
                << "the puppet rendered no visible pixels to the backbuffer";
        }
        wz::gpu::present(device, /*sync_interval*/ 0);

        // Offscreen render-to-texture (S6): render the SAME puppet into an RGBA8
        // render target and read it back. The puppet's Parts must leave
        // non-transparent pixels -- a real multi-draw (PSO + geometry) into a
        // texture, not just a clear. The RT is a 512x512 SQUARE, deliberately a
        // different size + aspect than the backbuffer, to prove per-target placement
        // (#280): the puppet is re-fitted to the target each render, so it lands.
        wz::gpu::TextureDesc rt_desc{};
        rt_desc.width = 512;
        rt_desc.height = 512;
        rt_desc.format = wz::gpu::TextureFormat::RGBA8Unorm;
        rt_desc.render_target = true;
        const wz::gpu::GPUHandle rt = wz::gpu::create_texture(device, rt_desc);
        ASSERT_TRUE(rt.valid());

        ASSERT_TRUE(wz::gpu::begin_frame(device));
        const bool rt_recorded = renderer.render_scene(
            nodes, assets, view_projection, camera_world_pos, {}, nullptr, rt);
        EXPECT_TRUE(rt_recorded)
            << "puppet failed to render into the offscreen target";
        ASSERT_TRUE(wz::gpu::end_frame(device));

        std::vector<std::uint8_t> pixels;
        ASSERT_TRUE(
            wz::gpu::dx12::internal::read_texture_rgba8_dx12(device, rt, pixels));
        ASSERT_EQ(pixels.size(),
            static_cast<std::size_t>(rt_desc.width) * rt_desc.height * 4u);
        // The target was cleared to (0,0,0,0); count texels the puppet touched (any
        // channel non-zero -- the premultiplied overlay blend leaves the visible
        // result in RGB).
        std::size_t drawn = 0;
        std::uint8_t max_channel = 0;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
            std::uint8_t m = pixels[i];
            if (pixels[i + 1] > m) m = pixels[i + 1];
            if (pixels[i + 2] > m) m = pixels[i + 2];
            if (pixels[i + 3] > m) m = pixels[i + 3];
            if (m > 8u) {
                ++drawn;
            }
            if (m > max_channel) max_channel = m;
        }
        EXPECT_GT(drawn, 0u)
            << "the puppet left no pixels in the offscreen render target "
               "(max channel value seen = " << static_cast<int>(max_channel) << ")";

        // #282: the animation clock belongs to the SIMULATION, not to
        // render_scene. An app that renders the same puppet more than once in a
        // frame (the showcase draws it to the backbuffer AND into an RTT) must
        // see the same instant every time -- so with no simulation_tick in
        // between, a second render into an identical target must reproduce the
        // pose byte for byte. Before the fix each render advanced the clock and
        // stepped the pendulums again, so this second image differed.
        const auto render_puppet_to_new_target =
            [&](std::vector<std::uint8_t>& out) -> bool
        {
            const wz::gpu::GPUHandle target =
                wz::gpu::create_texture(device, rt_desc);
            if (!target.valid() || !wz::gpu::begin_frame(device)) {
                return false;
            }
            const bool ok = renderer.render_scene(
                nodes, assets, view_projection, camera_world_pos, {}, nullptr,
                target);
            return ok && wz::gpu::end_frame(device)
                && wz::gpu::dx12::internal::read_texture_rgba8_dx12(
                    device, target, out);
        };
        const auto count_differing_texels =
            [](const std::vector<std::uint8_t>& a,
               const std::vector<std::uint8_t>& b) -> std::size_t
        {
            std::size_t differing = 0;
            const std::size_t n = a.size() < b.size() ? a.size() : b.size();
            for (std::size_t i = 0; i + 3 < n; i += 4) {
                if (a[i] != b[i] || a[i + 1] != b[i + 1]
                    || a[i + 2] != b[i + 2] || a[i + 3] != b[i + 3]) {
                    ++differing;
                }
            }
            return differing;
        };

        std::vector<std::uint8_t> same_instant;
        ASSERT_TRUE(render_puppet_to_new_target(same_instant));
        EXPECT_EQ(count_differing_texels(pixels, same_instant), 0u)
            << "re-rendering the puppet without a simulation_tick changed its "
               "pose -- render_scene is advancing the animation clock (#282)";

        // ...and once the simulation DOES advance, the puppet moves. ONE frame
        // of simulation is enough to change the image, which is what makes the
        // assertion above meaningful: before the fix the extra render advanced
        // the clock by exactly this much, so the two images differed.
        renderer.simulation_tick(1.0f / 60.0f);
        std::vector<std::uint8_t> later_instant;
        ASSERT_TRUE(render_puppet_to_new_target(later_instant));
        EXPECT_GT(count_differing_texels(pixels, later_instant), 0u)
            << "the puppet did not move after a frame of simulation -- the "
               "animation clock is not advancing at all";

        // First S6 consumer -- the 2D surface: display that offscreen texture on a
        // screen-space quad via the fullscreen blit, then read back the backbuffer.
        // The puppet must appear -- proving RTT -> sample-on-a-surface -> screen end
        // to end.
        ASSERT_TRUE(wz::gpu::begin_frame(device));
        EXPECT_TRUE(wz::gpu::dx12::internal::blit_texture_dx12(device, rt));
        ASSERT_TRUE(wz::gpu::end_frame(device));
        {
            std::vector<std::uint8_t> bb2;
            ASSERT_TRUE(
                wz::gpu::dx12::internal::read_backbuffer_rgba8_dx12(device, bb2));
            std::size_t lit = 0;
            for (std::size_t i = 0; i + 3 < bb2.size(); i += 4) {
                if (bb2[i] > 8u || bb2[i + 1] > 8u || bb2[i + 2] > 8u) {
                    ++lit;
                }
            }
            EXPECT_GT(lit, 0u)
                << "the puppet texture did not appear on the backbuffer via the "
                   "fullscreen blit";
        }
        wz::gpu::present(device, /*sync_interval*/ 0);

        // Third S6 consumer -- the 3D-mesh surface: display the SAME offscreen
        // texture on a quad transformed by an MVP (a 25-degree Y-tilt, scaled to
        // 0.8, pushed to mid-depth), then read back the backbuffer. Two things must
        // hold: bright puppet pixels appear (the textured-quad draw sampled the RTT),
        // and the screen corners outside the tilted quad stay at the clear colour
        // (it is a transformed *partial* surface, not the fullscreen blit).
        ASSERT_TRUE(wz::gpu::begin_frame(device));
        wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
        // Column-major MVP (the engine's shader convention): Y-rotation(25 deg) *
        // scale(0.8), with z translated to 0.5 so every corner lands in the [0,1]
        // clip-depth range (depth test is off, but the rasteriser still clips on z).
        const float mvp3d[16] = {
            0.72505f, 0.0f, -0.33810f, 0.0f,   // column 0
            0.0f,     0.8f,  0.0f,     0.0f,   // column 1
            0.33810f, 0.0f,  0.72505f, 0.0f,   // column 2
            0.0f,     0.0f,  0.5f,     1.0f,   // column 3
        };
        EXPECT_TRUE(
            wz::gpu::dx12::internal::draw_textured_quad_dx12(device, rt, mvp3d));
        ASSERT_TRUE(wz::gpu::end_frame(device));
        {
            std::vector<std::uint8_t> bb3;
            ASSERT_TRUE(
                wz::gpu::dx12::internal::read_backbuffer_rgba8_dx12(device, bb3));
            std::size_t bright = 0;           // puppet content, well above the clear
            std::size_t clear_remaining = 0;  // untouched corners outside the quad
            for (std::size_t i = 0; i + 3 < bb3.size(); i += 4) {
                if (bb3[i] > 80u || bb3[i + 1] > 80u || bb3[i + 2] > 80u) {
                    ++bright;
                }
                const int dr = static_cast<int>(bb3[i]) - 26;
                const int dg = static_cast<int>(bb3[i + 1]) - 26;
                const int db = static_cast<int>(bb3[i + 2]) - 31;
                if (!(dr > 15 || dr < -15 || dg > 15 || dg < -15
                      || db > 15 || db < -15)) {
                    ++clear_remaining;
                }
            }
            EXPECT_GT(bright, 0u)
                << "no bright puppet pixels appeared on the transformed 3D quad";
            EXPECT_GT(clear_remaining, 0u)
                << "the 3D quad covered the whole screen -- expected a transformed "
                   "partial surface with clear-coloured corners";
        }
        wz::gpu::present(device, /*sync_interval*/ 0);

        // World-surface mode: the same texture drawn as an in-scene surface
        // (premultiplied-alpha composite + depth test) over a NON-black background.
        // A full-screen quad (so every pixel is inside the card) over a green clear:
        // where the puppet is opaque the puppet shows; where it is transparent the
        // GREEN shows through -- proving alpha composites (an opaque card would leave
        // black there). Depth is cleared to 1.0 (far), so the quad at z=0.5 passes.
        ASSERT_TRUE(wz::gpu::begin_frame(device));
        wz::gpu::clear(device, 0.1f, 0.4f, 0.1f, 1.0f);  // distinct green backdrop
        const float mvp_fs[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,   // column 0
            0.0f, 1.0f, 0.0f, 0.0f,   // column 1
            0.0f, 0.0f, 0.0f, 0.0f,   // column 2 (quad z=0)
            0.0f, 0.0f, 0.5f, 1.0f,   // column 3: fill NDC, z=0.5
        };
        EXPECT_TRUE(wz::gpu::dx12::internal::draw_textured_quad_dx12(
            device, rt, mvp_fs,
            wz::gpu::dx12::internal::TexturedQuadMode::WorldSurface));
        ASSERT_TRUE(wz::gpu::end_frame(device));
        {
            std::vector<std::uint8_t> bb4;
            ASSERT_TRUE(
                wz::gpu::dx12::internal::read_backbuffer_rgba8_dx12(device, bb4));
            std::size_t bright = 0;      // opaque puppet content
            std::size_t backdrop = 0;    // green showing through transparent texels
            for (std::size_t i = 0; i + 3 < bb4.size(); i += 4) {
                const int r = bb4[i], g = bb4[i + 1], b = bb4[i + 2];
                if (r > 80 || g > 130 || b > 80) {
                    ++bright;
                }
                // near the green clear (~26,102,26): green dominant, red/blue low
                if (g > 70 && g < 140 && r < 60 && b < 60) {
                    ++backdrop;
                }
            }
            EXPECT_GT(bright, 0u)
                << "no puppet pixels composited on the world-surface quad";
            EXPECT_GT(backdrop, 0u)
                << "no green backdrop showed through -- the world-surface quad did "
                   "not alpha-composite (drew as an opaque card)";
        }
        wz::gpu::present(device, /*sync_interval*/ 0);
        wz::gpu::release_texture(device, rt);

        // ── B2-S1 pin (#311): the authored-RTT frame shape — an offscreen
        // pass AND the main pass in ONE begin/end_frame, exactly what
        // render_authored_render_targets + render_scene produce. The puppet VS
        // converts its pixel-space affine (CPU-packed per pass) to NDC by
        // dividing by the SCREEN buffer's viewport, read at EXECUTE time; both
        // passes refresh that one buffer. With an immediate (one-shot) refresh
        // every draw read the LAST pass's dims: the offscreen puppet collapsed
        // to a corner sliver (measured 43 px at x=[3,8] y=[3,17] of 128²) and
        // masked Parts vanished (mask_uv aliased too). The refresh is now a
        // RECORDED copy ordered with the frame, so each pass reads its own
        // dims and the fit+centered puppet must span the target's midline.
        {
            wz::gpu::TextureDesc s1_desc{};
            s1_desc.width = 128;
            s1_desc.height = 128;
            s1_desc.format = wz::gpu::TextureFormat::RGBA8Unorm;
            s1_desc.render_target = true;
            const wz::gpu::GPUHandle s1_rt =
                wz::gpu::create_texture(device, s1_desc);
            ASSERT_TRUE(s1_rt.valid());

            ASSERT_TRUE(wz::gpu::begin_frame(device));
            wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
            EXPECT_TRUE(renderer.render_scene(
                nodes, assets, view_projection, camera_world_pos, {}, nullptr,
                s1_rt));
            EXPECT_TRUE(renderer.render_scene(
                nodes, assets, view_projection, camera_world_pos));
            ASSERT_TRUE(wz::gpu::end_frame(device));

            std::vector<std::uint8_t> s1_px;
            ASSERT_TRUE(wz::gpu::dx12::internal::read_texture_rgba8_dx12(
                device, s1_rt, s1_px));
            ASSERT_EQ(s1_px.size(), static_cast<std::size_t>(128) * 128 * 4);
            std::uint32_t min_x = 128, max_x = 0, min_y = 128, max_y = 0;
            std::size_t s1_drawn = 0;
            for (std::uint32_t y = 0; y < 128; ++y) {
                for (std::uint32_t x = 0; x < 128; ++x) {
                    const std::size_t i =
                        (static_cast<std::size_t>(y) * 128 + x) * 4;
                    std::uint8_t m = s1_px[i];
                    if (s1_px[i + 1] > m) m = s1_px[i + 1];
                    if (s1_px[i + 2] > m) m = s1_px[i + 2];
                    if (s1_px[i + 3] > m) m = s1_px[i + 3];
                    if (m > 8u) {
                        ++s1_drawn;
                        if (x < min_x) min_x = x;
                        if (x > max_x) max_x = x;
                        if (y < min_y) min_y = y;
                        if (y > max_y) max_y = y;
                    }
                }
            }
            ASSERT_GT(s1_drawn, 0u)
                << "two-pass frame: nothing drawn into the offscreen target";
            EXPECT_GT(max_x, 68u)
                << "offscreen pass drew with the MAIN pass's screen constants "
                   "(B2-S1 regressed); bbox x=[" << min_x << "," << max_x
                << "] y=[" << min_y << "," << max_y << "] drawn=" << s1_drawn
                << " of 128x128";
            EXPECT_GT(max_y, 68u)
                << "offscreen puppet confined to the top of its target "
                   "(B2-S1 regressed); bbox x=[" << min_x << "," << max_x
                << "] y=[" << min_y << "," << max_y << "] drawn=" << s1_drawn;

            // The main pass of the same frame still renders the puppet with
            // ITS dims — the recorded refresh must not starve later passes.
            std::vector<std::uint8_t> s1_bb;
            ASSERT_TRUE(wz::gpu::dx12::internal::read_backbuffer_rgba8_dx12(
                device, s1_bb));
            std::size_t s1_nonclear = 0;
            for (std::size_t i = 0; i + 3 < s1_bb.size(); i += 4) {
                const int dr = static_cast<int>(s1_bb[i]) - 26;
                const int dg = static_cast<int>(s1_bb[i + 1]) - 26;
                const int db = static_cast<int>(s1_bb[i + 2]) - 31;
                if (dr > 15 || dr < -15 || dg > 15 || dg < -15
                    || db > 15 || db < -15) {
                    ++s1_nonclear;
                }
            }
            EXPECT_GT(s1_nonclear, 0u)
                << "the MAIN pass of the two-pass frame rendered nothing";
            wz::gpu::present(device, /*sync_interval*/ 0);

            // ── B2-H6 pin (#311): mask sets are cached PER SIZE. The frame
            // above rendered the puppet at 128 then 256; before the fix each
            // size change released and re-created the whole mask set — twice
            // per frame, forever. A second identical two-pass frame must find
            // both sizes' sets resident and build NOTHING.
            const std::uint64_t mask_builds =
                renderer.puppet_mask_set_builds();
            EXPECT_GT(mask_builds, 0u)
                << "no mask sets were ever built — Aka's masked Parts should "
                   "have forced at least one";
            ASSERT_TRUE(wz::gpu::begin_frame(device));
            wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
            EXPECT_TRUE(renderer.render_scene(
                nodes, assets, view_projection, camera_world_pos, {}, nullptr,
                s1_rt));
            EXPECT_TRUE(renderer.render_scene(
                nodes, assets, view_projection, camera_world_pos));
            ASSERT_TRUE(wz::gpu::end_frame(device));
            EXPECT_EQ(renderer.puppet_mask_set_builds(), mask_builds)
                << "mask sets were rebuilt for already-seen sizes — the "
                   "per-pass create/release thrash is back (B2-H6)";
            wz::gpu::present(device, /*sync_interval*/ 0);
            wz::gpu::release_texture(device, s1_rt);
        }

        // Structural wiring proofs:
        //  - the puppet program realized from the asset compiler (no render-time
        //    bridge), like the splat/clipmap tests assert,
        EXPECT_GT(renderer.registered_program_count(), 0u);
        EXPECT_EQ(renderer.render_time_program_bridge_count(), 0u)
            << "puppet program was bridged at render time, not produced by the "
               "asset compiler";
        //  - the per-Part object SRGs bound descriptor tables (Aka has many
        //    Parts, so the puppet records many packets).
        EXPECT_GT(renderer.cached_descriptor_table_count(), 0u)
            << "puppet Part object SRGs did not bind descriptor tables";

        // ── #317 pin: a graph swap must release the puppet's mask render
        // targets. They are acquired by render_puppet_masks, referenced by
        // nothing else, and tracked by no compiler -- so if the renderer does
        // not release them, NOTHING can: on_graph_changed used to release four
        // named handle fields and only when owns_buffers was true, and the
        // puppet sets owns_buffers FALSE (its pull buffers are asset-owned),
        // so the whole renderable was skipped and realized_renderables_.clear()
        // then destroyed the last handle to them. ~133 MB per swap for Aka.
        //
        // The assertion is a strict decrease rather than an exact count because
        // the number of distinct mask sources is fixture data; before the fix
        // the count did not move AT ALL, which is what this catches. The
        // puppet's pull buffers are asset-owned and are NOT released here, so
        // any drop is the mask targets.
        const std::size_t resident_before = renderer.resident_gpu_resource_count();
        EXPECT_GT(resident_before, 0u);

        renderer.on_graph_changed();

        const std::size_t resident_after = renderer.resident_gpu_resource_count();
        EXPECT_LT(resident_after, resident_before)
            << "on_graph_changed released nothing for the puppet -- its mask "
               "render targets are now unreachable and leaked (resident "
            << resident_before << " -> " << resident_after << ")";

        // Idempotent: the handles were cleared, so a second swap must not
        // double-release (which would return them to the free list twice).
        renderer.on_graph_changed();
        EXPECT_EQ(renderer.resident_gpu_resource_count(), resident_after)
            << "a second graph swap released the mask targets again";
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
