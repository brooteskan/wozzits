#include <gtest/gtest.h>

#include <engine/assets/authoring/asset_graph_authoring.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/engine_gpu_context.h>

#include <asset/draft.h>
#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <window/window2.h>

#include <wozzits/rhi/gpu_resource_registry.h>

#include <cstdint>
#include <string>

// Coverage for the render-target texture recipe (#281): a SOURCE-LESS
// kAssetTypeTexture whose dimensions are authored rather than decoded, made
// resident Sampled | RenderTarget so a pass can draw into what a material
// samples.
//
// The load-bearing assertion is the IDENTITY: it publishes under the same
// rhi_asset_identity(key, "texture") discriminator a file-backed texture uses,
// which is exactly why an authored render binding can name it wherever a
// texture is accepted, with no new entry in render_binding_sources.h. If that
// discriminator ever drifts, a material binding resolves to nothing and the
// surface samples garbage rather than failing loudly.

namespace
{
    namespace ea = wz::engine::assets;

    wz::fs::Path make_root(const char* suffix)
    {
        const wz::fs::Path root = wz::fs::join(
            wz::fs::temp_directory_path(),
            std::string("wozzits_render_target_texture_") + suffix);
        EXPECT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);
        return root;
    }

    // Author one render-target texture node with the given dimensions and
    // commit it. Returns the node's resolved AssetKey (empty if authoring
    // failed), so the caller can look up what it published.
    wz::asset::AssetKey author_render_target(
        ea::EngineAssetLibrary& assets,
        std::int64_t width,
        std::int64_t height,
        bool& out_resolved)
    {
        wz::asset::AssetGraphDraft draft{};
        wz::asset::load_asset_graph_draft_from_registered_assets(
            draft, assets.system().registered_assets());

        wz::asset::ParamBlock params;
        params.values["width"] = width;
        params.values["height"] = height;

        const wz::asset::AssetGraphDraftNodeId node =
            ea::authoring::add_source_asset_node(
                draft,
                assets.graph_authoring_context(),
                ea::kRenderTargetTextureSchema,
                ea::kAssetTypeTexture,
                params);
        EXPECT_NE(node, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);

        const auto commit = assets.commit_asset_graph_draft(draft);
        EXPECT_TRUE(commit.success());

        out_resolved = assets.resolve_all().ok();

        const wz::asset::AssetGraphDraftNode* n =
            wz::asset::find_asset_graph_draft_node(draft, node);
        return n ? n->node.key : wz::asset::AssetKey{};
    }
}

TEST(RenderTargetTexture, PublishesSampleableRenderTargetUnderTextureVariant)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "render_target_texture_test";
    window_desc.width = 64;
    window_desc.height = 64;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for on-device registry test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for on-device registry test";
    }

    {
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;
        ea::EngineAssetLibrary assets(gpu, logger, make_root("publish"));

        bool resolved = false;
        const wz::asset::AssetKey key =
            author_render_target(assets, 256, 128, resolved);
        ASSERT_FALSE(key == wz::asset::AssetKey{});
        ASSERT_TRUE(resolved) << "render target texture did not resolve";

        // Same discriminator a file-backed texture publishes under -- this is
        // what lets a material binding name it with no new binding source.
        const wz::rhi::ResourceIdentity identity{
            ea::rhi_asset_identity(key, "texture"), {} };
        const wz::rhi::GpuResourceHandle handle =
            gpu.resources.find(identity);
        ASSERT_TRUE(handle.valid())
            << "render target texture is not resident under the \"texture\" "
               "variant a render binding resolves against";

        const wz::rhi::GpuResource* resource = gpu.resources.get(handle);
        ASSERT_NE(resource, nullptr);

        // BOTH usages, and that pairing is the whole point: RenderTarget alone
        // could not be sampled by the material, Sampled alone could not be
        // rendered into by the compositor.
        EXPECT_NE(
            resource->desc.usage & wz::rhi::ResourceUsage_RenderTarget, 0u)
            << "not usable as a render target";
        EXPECT_NE(resource->desc.usage & wz::rhi::ResourceUsage_Sampled, 0u)
            << "not sampleable";

        // Authored dimensions reach the GPU resource, not the 512 default.
        EXPECT_EQ(resource->desc.width, 256u);
        EXPECT_EQ(resource->desc.height, 128u);
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}

TEST(RenderTargetTexture, RejectsDegenerateDimensions)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "render_target_texture_reject_test";
    window_desc.width = 64;
    window_desc.height = 64;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for on-device registry test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for on-device registry test";
    }

    {
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;
        ea::EngineAssetLibrary assets(gpu, logger, make_root("reject"));

        // A zero extent is an authoring slip. It must FAIL the compile with a
        // reason rather than acquiring a degenerate resource -- the inspector
        // surfaces the reason, and a silently-zero-sized material would show up
        // much later as a surface sampling nothing.
        bool resolved = true;
        const wz::asset::AssetKey key =
            author_render_target(assets, 0, 128, resolved);
        EXPECT_FALSE(resolved)
            << "a zero-width render target texture must not resolve";
        if (!(key == wz::asset::AssetKey{})) {
            EXPECT_FALSE(
                gpu.resources
                    .find(wz::rhi::ResourceIdentity{
                        ea::rhi_asset_identity(key, "texture"), {} })
                    .valid())
                << "a rejected render target must publish nothing";
        }
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
