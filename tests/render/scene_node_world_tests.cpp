// tests/render/scene_node_world_tests.cpp
//
// Unit coverage for the RHI render path's device-free helpers. Pure CPU: no
// device, so these always run rather than skipping on a machine without a GPU.
//
//   * compute_scene_node_world_transforms / _effective_visibility (the
//     hierarchical transform + visibility composition): children inherit
//     parent translation + scale, the result is independent of node order,
//     chains compose fully, and dangling / cyclic parents resolve safely.
//   * make_view_constants (the VIEW-frequency block): its byte layout matches
//     the WzViewConstants struct the binding prelude emits, and it packs the
//     observer + the frame's atmosphere into it.

#include <engine/rendering/rhi_scene_renderer.h>

#include <engine/assets/atmosphere/atmosphere.h>
#include <engine/assets/scene/scene_asset_data.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace
{
    namespace ea = wz::engine::assets;
    namespace er = wz::engine::rendering;

    ea::SceneNodeAsset node(
        std::string id,
        std::optional<std::string> parent,
        float tx,
        float ty,
        float tz,
        float scale = 1.0f)
    {
        ea::SceneNodeAsset n{};
        n.id = std::move(id);
        n.parent_id = std::move(parent);
        n.local.translation[0] = tx;
        n.local.translation[1] = ty;
        n.local.translation[2] = tz;
        n.local.scale[0] = scale;
        n.local.scale[1] = scale;
        n.local.scale[2] = scale;
        return n;  // identity rotation (AuthoredTransform default quat)
    }
}

TEST(SceneNodeWorld, RootUsesItsOwnLocal)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("a", std::nullopt, 5.0f, 6.0f, 7.0f),
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    ASSERT_EQ(world.size(), 1u);
    EXPECT_FLOAT_EQ(world[0].m[12], 5.0f);
    EXPECT_FLOAT_EQ(world[0].m[13], 6.0f);
    EXPECT_FLOAT_EQ(world[0].m[14], 7.0f);
}

TEST(SceneNodeWorld, ChildInheritsParentTranslation)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("p", std::nullopt, 10.0f, 0.0f, 0.0f),
        node("c", "p", 1.0f, 0.0f, 0.0f),
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    EXPECT_FLOAT_EQ(world[1].m[12], 11.0f);  // 10 (parent) + 1 (child)
}

TEST(SceneNodeWorld, ChildInheritsParentScale)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("p", std::nullopt, 0.0f, 0.0f, 0.0f, 2.0f),  // 2x scale
        node("c", "p", 1.0f, 0.0f, 0.0f),                 // local +1 in x
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    EXPECT_FLOAT_EQ(world[1].m[0], 2.0f);    // world scale = parent scale
    EXPECT_FLOAT_EQ(world[1].m[12], 2.0f);   // child offset scaled by parent: 2*1
}

TEST(SceneNodeWorld, OrderIndependentChildBeforeParent)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("c", "p", 1.0f, 0.0f, 0.0f),
        node("p", std::nullopt, 10.0f, 0.0f, 0.0f),
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    EXPECT_FLOAT_EQ(world[0].m[12], 11.0f);  // child (index 0) still composes
}

TEST(SceneNodeWorld, GrandchildComposesWholeChain)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("a", std::nullopt, 100.0f, 0.0f, 0.0f),
        node("b", "a", 10.0f, 0.0f, 0.0f),
        node("c", "b", 1.0f, 0.0f, 0.0f),
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    EXPECT_FLOAT_EQ(world[2].m[12], 111.0f);
}

TEST(SceneNodeWorld, DanglingParentFallsBackToLocal)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("c", "missing", 3.0f, 0.0f, 0.0f),
    };
    const auto world = er::compute_scene_node_world_transforms(nodes);
    ASSERT_EQ(world.size(), 1u);
    EXPECT_FLOAT_EQ(world[0].m[12], 3.0f);
}

TEST(SceneNodeWorld, ParentCycleTerminates)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("a", "b", 1.0f, 0.0f, 0.0f),
        node("b", "a", 2.0f, 0.0f, 0.0f),
    };
    // Must not hang or crash; values are cycle-broken but finite.
    const auto world = er::compute_scene_node_world_transforms(nodes);
    ASSERT_EQ(world.size(), 2u);
}

TEST(SceneNodeWorld, EmptySceneYieldsEmpty)
{
    const std::vector<ea::SceneNodeAsset> nodes;
    const auto world = er::compute_scene_node_world_transforms(nodes);
    EXPECT_TRUE(world.empty());
}

// ── Inherited visibility (compute_scene_node_effective_visibility) ──────────

TEST(SceneNodeVisibility, HiddenParentHidesChildSubtree)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("host", std::nullopt, 0.0f, 0.0f, 0.0f),
        node("host/body", "host", 0.0f, 0.0f, 0.0f),
        node("host/body/turret", "host/body", 0.0f, 0.0f, 0.0f),
    };
    nodes[0].visible = false;  // hide the host
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    ASSERT_EQ(vis.size(), 3u);
    EXPECT_EQ(vis[0], 0u);  // host hidden
    EXPECT_EQ(vis[1], 0u);  // child inherits hidden
    EXPECT_EQ(vis[2], 0u);  // grandchild inherits hidden
}

TEST(SceneNodeVisibility, VisibleParentLeavesChildrenVisible)
{
    const std::vector<ea::SceneNodeAsset> nodes{
        node("host", std::nullopt, 0.0f, 0.0f, 0.0f),
        node("host/body", "host", 0.0f, 0.0f, 0.0f),
    };
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    EXPECT_EQ(vis[0], 1u);
    EXPECT_EQ(vis[1], 1u);
}

TEST(SceneNodeVisibility, OwnHiddenDoesNotHideSiblings)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("root", std::nullopt, 0.0f, 0.0f, 0.0f),
        node("a", "root", 0.0f, 0.0f, 0.0f),
        node("b", "root", 0.0f, 0.0f, 0.0f),
    };
    nodes[1].visible = false;  // hide 'a' only
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    EXPECT_EQ(vis[0], 1u);  // root visible
    EXPECT_EQ(vis[1], 0u);  // a hidden
    EXPECT_EQ(vis[2], 1u);  // sibling b unaffected
}

TEST(SceneNodeVisibility, OrderIndependentChildBeforeHiddenParent)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("c", "p", 0.0f, 0.0f, 0.0f),
        node("p", std::nullopt, 0.0f, 0.0f, 0.0f),
    };
    nodes[1].visible = false;  // hide parent (listed after the child)
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    EXPECT_EQ(vis[0], 0u);  // child still inherits the hidden parent
    EXPECT_EQ(vis[1], 0u);
}

TEST(SceneNodeVisibility, DanglingParentUsesOwnVisibility)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("c", "missing", 0.0f, 0.0f, 0.0f),
    };
    nodes[0].visible = true;
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    ASSERT_EQ(vis.size(), 1u);
    EXPECT_EQ(vis[0], 1u);  // no usable parent -> own visibility wins
}

TEST(SceneNodeVisibility, ParentCycleTerminates)
{
    std::vector<ea::SceneNodeAsset> nodes{
        node("a", "b", 0.0f, 0.0f, 0.0f),
        node("b", "a", 0.0f, 0.0f, 0.0f),
    };
    nodes[0].visible = false;
    const auto vis = er::compute_scene_node_effective_visibility(nodes);
    ASSERT_EQ(vis.size(), 2u);  // must not hang
}

// ─── View-frequency constants ───────────────────────────────────────────────
//
// The shader side of this contract is generated text, not code anything here
// can call, so the two halves can only be held together by agreeing on a byte
// layout. These pin the CPU half against what render_binding_layout.cpp emits
// for RenderBindingViewHead::CameraFog:
//
//     struct WzViewConstants
//     {
//         float4 camera;      // xyz = camera world position
//         float4 fog_color;   // rgb = colour, w = density
//         float4 fog_params;  // x = start, y = height falloff, z = enabled
//     };
//
// A drift here is invisible until a shader silently reads the wrong dial.

TEST(ViewConstants, LayoutMatchesTheGeneratedHlslStruct)
{
    // 12 dwords, three float4s, no padding between them: HLSL 16-byte-aligns
    // each float4 and so does this, which is why the tight C++ struct and the
    // StructuredBuffer element agree without any packing gymnastics.
    EXPECT_EQ(sizeof(er::ViewConstants), 48u);
    EXPECT_EQ(offsetof(er::ViewConstants, camera), 0u);
    EXPECT_EQ(offsetof(er::ViewConstants, fog_color), 16u);
    EXPECT_EQ(offsetof(er::ViewConstants, fog_params), 32u);
    EXPECT_EQ(sizeof(er::ViewConstants::camera), 16u);
    EXPECT_EQ(sizeof(er::ViewConstants::fog_color), 16u);
    EXPECT_EQ(sizeof(er::ViewConstants::fog_params), 16u);
}

TEST(ViewConstants, NullAtmosphereDisablesFogButStillCarriesTheCamera)
{
    const wz::math::Vec3 camera{ 3.0f, -4.0f, 5.0f };
    const er::ViewConstants v = er::make_view_constants(camera, nullptr);

    // The camera is VIEW state, not fog state: a program reading the observer
    // must get it whether or not an atmosphere was authored.
    EXPECT_FLOAT_EQ(v.camera[0], 3.0f);
    EXPECT_FLOAT_EQ(v.camera[1], -4.0f);
    EXPECT_FLOAT_EQ(v.camera[2], 5.0f);
    EXPECT_FLOAT_EQ(v.camera[3], 0.0f);   // unused

    // fog_params.z is the flag wz_fog_from_view tests before doing anything.
    // Off, not "on with zero density" -- the helper returns early.
    EXPECT_FLOAT_EQ(v.fog_params[2], 0.0f);
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(v.fog_color[i], 0.0f) << "fog_color[" << i << "]";
    }
    EXPECT_FLOAT_EQ(v.fog_params[0], 0.0f);
    EXPECT_FLOAT_EQ(v.fog_params[1], 0.0f);
    EXPECT_FLOAT_EQ(v.fog_params[3], 0.0f);
}

TEST(ViewConstants, PopulatedAtmospherePacksEveryDialAtItsOwnSlot)
{
    ea::AtmosphereData atmosphere{};
    atmosphere.fog_color[0] = 0.1f;
    atmosphere.fog_color[1] = 0.2f;
    atmosphere.fog_color[2] = 0.3f;
    atmosphere.fog_density = 0.04f;
    atmosphere.fog_start_distance = 250.0f;
    atmosphere.fog_height_falloff = 0.75f;
    atmosphere.fog_enabled = true;
    atmosphere.fog_saturation = 1.5f;
    ASSERT_TRUE(atmosphere.valid());

    const wz::math::Vec3 camera{ -11.0f, 22.0f, -33.0f };
    const er::ViewConstants v = er::make_view_constants(camera, &atmosphere);

    EXPECT_FLOAT_EQ(v.camera[0], -11.0f);
    EXPECT_FLOAT_EQ(v.camera[1], 22.0f);
    EXPECT_FLOAT_EQ(v.camera[2], -33.0f);

    // Every value distinct, so a transposed pair fails rather than passing by
    // coincidence: colour in rgb, DENSITY in w (not a fourth colour channel).
    EXPECT_FLOAT_EQ(v.fog_color[0], 0.1f);
    EXPECT_FLOAT_EQ(v.fog_color[1], 0.2f);
    EXPECT_FLOAT_EQ(v.fog_color[2], 0.3f);
    EXPECT_FLOAT_EQ(v.fog_color[3], 0.04f);

    EXPECT_FLOAT_EQ(v.fog_params[0], 250.0f);   // start distance
    EXPECT_FLOAT_EQ(v.fog_params[1], 0.75f);    // height falloff
    EXPECT_FLOAT_EQ(v.fog_params[2], 1.0f);     // enabled
    EXPECT_FLOAT_EQ(v.fog_params[3], 1.5f);     // saturation
}

TEST(ViewConstants, AuthoredButDisabledAtmosphereKeepsItsDialsAndStaysOff)
{
    // Authoring the dials with the fog switched off is the documented way to
    // stage an atmosphere (see AtmosphereData::valid). The values must survive
    // the pack -- only the flag says whether the shader uses them.
    ea::AtmosphereData atmosphere{};
    atmosphere.fog_color[0] = 0.9f;
    atmosphere.fog_density = 0.5f;
    atmosphere.fog_start_distance = 10.0f;
    atmosphere.fog_enabled = false;

    const er::ViewConstants v =
        er::make_view_constants(wz::math::Vec3{ 0.0f, 0.0f, 0.0f }, &atmosphere);

    EXPECT_FLOAT_EQ(v.fog_params[2], 0.0f);   // off
    EXPECT_FLOAT_EQ(v.fog_color[0], 0.9f);    // but not erased
    EXPECT_FLOAT_EQ(v.fog_color[3], 0.5f);
    EXPECT_FLOAT_EQ(v.fog_params[0], 10.0f);
}
