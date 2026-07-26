#include <gtest/gtest.h>

#include <engine/assets/authoring/asset_graph_authoring.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/texture/texture.h>
#include <engine/assets/texture_asset_module.h>
#include <engine/assets/type_extensions.h>

#include <asset/draft.h>
#include <file/filesystem.h>
#include <gpu/gpu.h>

#include <string>

// Issue #285: the composite material as an AUTHORED asset. #281 proved the
// chain -- a base colour with art placed over it, sampled by a lit surface --
// but every decision in it lived in app C++: which art, what colour, where it
// sits. None of those could be changed without a rebuild, which made it a demo
// rather than a capability.
//
// These tests are device-free: they assert the AUTHORING contract (params and
// layer ports become the recipe the compositor is handed), which is the part
// that was missing. The compositing itself is #281's, already covered.

namespace
{
    namespace ea = wz::engine::assets;

    wz::fs::Path make_root(const char* suffix)
    {
        const wz::fs::Path root = wz::fs::join(
            wz::fs::temp_directory_path(),
            std::string("wozzits_composite_material_") + suffix);
        EXPECT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);
        return root;
    }

    // A source texture for a layer: a render target, since the motivating case
    // is a live one (#287) rather than a file-backed image.
    wz::asset::AssetGraphDraftNodeId add_render_target(
        wz::asset::AssetGraphDraft& draft,
        ea::EngineAssetLibrary& assets,
        const char* name)
    {
        wz::asset::ParamBlock params;
        params.values["name"] = std::string(name);
        params.values["width"] = std::int64_t{ 64 };
        params.values["height"] = std::int64_t{ 64 };
        return ea::authoring::add_source_asset_node(
            draft,
            assets.graph_authoring_context(),
            ea::kRenderTargetTextureSchema,
            ea::kAssetTypeTexture,
            params);
    }

    const ea::TextureData* compiled_texture(
        ea::EngineAssetLibrary& assets, const wz::asset::AssetKey& key)
    {
        const auto* compiled = assets.system().find_compiled(key);
        if (!compiled) {
            return nullptr;
        }
        return assets.textures().get_texture_data(
            ea::TextureHandle{ compiled->handle });
    }
}

TEST(CompositeMaterial, AuthoredParamsAndPortsBecomeTheRecipe)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    ea::EngineAssetLibrary assets(device, logger, make_root("recipe"));

    wz::asset::AssetGraphDraft draft{};

    const auto art = add_render_target(draft, assets, "art");
    ASSERT_NE(art, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);

    wz::asset::ParamBlock params;
    params.values["name"] = std::string("sphere_material");
    params.values["width"] = std::int64_t{ 256 };
    params.values["height"] = std::int64_t{ 128 };
    params.values["base_colour"] =
        std::array<float, 3>{ 0.62f, 0.62f, 0.65f };
    params.values["base_alpha"] = 1.0;
    params.values["layer0_centre_u"] = 0.25;
    params.values["layer0_centre_v"] = 0.75;
    params.values["layer0_half_width"] = 0.35;
    params.values["layer0_half_height"] = 0.20;
    params.values["layer0_rotation"] = 0.5;
    params.values["layer0_opacity"] = 0.8;

    const auto material = ea::authoring::add_source_asset_node(
        draft,
        assets.graph_authoring_context(),
        ea::kCompositeMaterialTextureSchema,
        ea::kAssetTypeTexture,
        params);
    ASSERT_NE(material, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);

    ASSERT_NE(
        wz::asset::connect_asset_graph_draft_nodes(draft, art, material, 0u),
        wz::asset::INVALID_ASSET_GRAPH_DRAFT_EDGE);

    const auto commit = assets.commit_asset_graph_draft(draft);
    ASSERT_TRUE(commit.success());
    ASSERT_TRUE(assets.resolve_all().ok());

    const wz::asset::AssetGraphDraftNode* material_node =
        wz::asset::find_asset_graph_draft_node(draft, material);
    ASSERT_NE(material_node, nullptr);
    const ea::TextureData* recipe =
        compiled_texture(assets, material_node->node.key);
    ASSERT_NE(recipe, nullptr);

    // Still an ordinary texture to everyone else -- same type, same metadata --
    // which is why a material binds one with no special case.
    EXPECT_EQ(recipe->width, 256u);
    EXPECT_EQ(recipe->height, 128u);
    EXPECT_TRUE(recipe->valid());

    EXPECT_TRUE(recipe->is_composite);
    EXPECT_FLOAT_EQ(recipe->base_colour[0], 0.62f);
    EXPECT_FLOAT_EQ(recipe->base_colour[1], 0.62f);
    EXPECT_FLOAT_EQ(recipe->base_colour[2], 0.65f);
    EXPECT_FLOAT_EQ(recipe->base_colour[3], 1.0f);

    // Every authored dial reaches the layer, including the ones #281 could only
    // change by editing C++.
    ASSERT_EQ(recipe->composite_layers.size(), 1u);
    const ea::CompositeMaterialLayer& layer = recipe->composite_layers[0];
    EXPECT_FLOAT_EQ(layer.centre_uv[0], 0.25f);
    EXPECT_FLOAT_EQ(layer.centre_uv[1], 0.75f);
    EXPECT_FLOAT_EQ(layer.half_size_uv[0], 0.35f);
    EXPECT_FLOAT_EQ(layer.half_size_uv[1], 0.20f);
    EXPECT_FLOAT_EQ(layer.rotation, 0.5f);
    EXPECT_FLOAT_EQ(layer.opacity, 0.8f);

    // The layer's SOURCE came from the wired port, not from a param -- this is
    // the half that makes "which art" authorable.
    const wz::asset::AssetGraphDraftNode* art_node =
        wz::asset::find_asset_graph_draft_node(draft, art);
    ASSERT_NE(art_node, nullptr);
    EXPECT_TRUE(layer.source == art_node->node.key);
}

// Layer ports are optional and independent: wiring only port 1 must produce
// layer 1's authored transform, not port 0's defaults. Dep positions shift when
// an optional port is unwired, which is exactly how a naive dep-order read gets
// this wrong.
TEST(CompositeMaterial, UnwiredLayerPortsAreSkippedNotShifted)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    ea::EngineAssetLibrary assets(device, logger, make_root("ports"));

    wz::asset::AssetGraphDraft draft{};
    const auto art = add_render_target(draft, assets, "art");

    wz::asset::ParamBlock params;
    params.values["name"] = std::string("second_slot_only");
    params.values["width"] = std::int64_t{ 64 };
    params.values["height"] = std::int64_t{ 64 };
    // Port 0 is left unwired; the values below belong to port 1.
    params.values["layer0_centre_u"] = 0.1;
    params.values["layer1_centre_u"] = 0.9;
    params.values["layer1_opacity"] = 0.5;

    const auto material = ea::authoring::add_source_asset_node(
        draft,
        assets.graph_authoring_context(),
        ea::kCompositeMaterialTextureSchema,
        ea::kAssetTypeTexture,
        params);
    ASSERT_NE(
        wz::asset::connect_asset_graph_draft_nodes(draft, art, material, 1u),
        wz::asset::INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(assets.commit_asset_graph_draft(draft).success());
    ASSERT_TRUE(assets.resolve_all().ok());

    const wz::asset::AssetGraphDraftNode* material_node =
        wz::asset::find_asset_graph_draft_node(draft, material);
    ASSERT_NE(material_node, nullptr);
    const ea::TextureData* recipe =
        compiled_texture(assets, material_node->node.key);
    ASSERT_NE(recipe, nullptr);

    ASSERT_EQ(recipe->composite_layers.size(), 1u);
    EXPECT_FLOAT_EQ(recipe->composite_layers[0].centre_uv[0], 0.9f)
        << "the wired port's transform was read from the wrong layer index";
    EXPECT_FLOAT_EQ(recipe->composite_layers[0].opacity, 0.5f);
}

// A composite with no layers is legal and useful: a flat authored colour a
// surface can sample, and the state a material is in before any art is wired.
TEST(CompositeMaterial, NoLayersIsAPlainAuthoredColour)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    ea::EngineAssetLibrary assets(device, logger, make_root("bare"));

    wz::asset::AssetGraphDraft draft{};
    wz::asset::ParamBlock params;
    params.values["name"] = std::string("flat");
    params.values["base_colour"] = std::array<float, 3>{ 1.0f, 0.0f, 0.5f };

    const auto material = ea::authoring::add_source_asset_node(
        draft,
        assets.graph_authoring_context(),
        ea::kCompositeMaterialTextureSchema,
        ea::kAssetTypeTexture,
        params);
    ASSERT_TRUE(assets.commit_asset_graph_draft(draft).success());
    ASSERT_TRUE(assets.resolve_all().ok());

    const wz::asset::AssetGraphDraftNode* node =
        wz::asset::find_asset_graph_draft_node(draft, material);
    ASSERT_NE(node, nullptr);
    const ea::TextureData* recipe = compiled_texture(assets, node->node.key);
    ASSERT_NE(recipe, nullptr);
    EXPECT_TRUE(recipe->is_composite);
    EXPECT_TRUE(recipe->composite_layers.empty());
    EXPECT_FLOAT_EQ(recipe->base_colour[0], 1.0f);
    EXPECT_FLOAT_EQ(recipe->base_colour[2], 0.5f);
    // Defaults fill the rest, so a bare node still produces a usable texture.
    EXPECT_EQ(recipe->width, 512u);
    EXPECT_EQ(recipe->height, 512u);
}

// Two composites of the same size must NOT collide. A composited texture is a
// SINK: an authored node's key derives from its params, so without the `name`
// param both would derive one key and the commit batch would reject the graph
// -- the failure the puppet-card conversion hit for real.
TEST(CompositeMaterial, TwoMaterialsOfTheSameSizeCoexist)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    ea::EngineAssetLibrary assets(device, logger, make_root("distinct"));

    wz::asset::AssetGraphDraft draft{};
    const auto add = [&](const char* name)
    {
        wz::asset::ParamBlock params;
        params.values["name"] = std::string(name);
        params.values["width"] = std::int64_t{ 128 };
        params.values["height"] = std::int64_t{ 128 };
        return ea::authoring::add_source_asset_node(
            draft,
            assets.graph_authoring_context(),
            ea::kCompositeMaterialTextureSchema,
            ea::kAssetTypeTexture,
            params);
    };

    const auto first = add("wall");
    const auto second = add("floor");

    ASSERT_TRUE(assets.commit_asset_graph_draft(draft).success())
        << "two same-size composite materials were rejected as duplicates";
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* a = wz::asset::find_asset_graph_draft_node(draft, first);
    const auto* b = wz::asset::find_asset_graph_draft_node(draft, second);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_FALSE(a->node.key == b->node.key);
    EXPECT_NE(compiled_texture(assets, a->node.key), nullptr);
    EXPECT_NE(compiled_texture(assets, b->node.key), nullptr);
}
