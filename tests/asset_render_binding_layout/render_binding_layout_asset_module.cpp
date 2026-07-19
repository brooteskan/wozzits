#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/render_binding_layout.h>
#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/type_extensions.h>

#include <engine/assets/render_binding_layout/render_binding_constants.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <string>

namespace
{
    using namespace wz::engine::assets;

    // The preset-2 (clipmap landscape) SRG expressed as a layout: the shape
    // the issue #227 seam test reproduces through a custom render program.
    RenderBindingLayoutData clipmap_layout()
    {
        RenderBindingLayoutData layout{};
        layout.constants_semantic = "clipmap";
        layout.constants_visibility = ShaderVisibility::All;
        layout.constants_head = RenderBindingConstantsHead::CameraSnappedTerrain;
        layout.bindings = {
            {
                .semantic = "pulled_mesh_positions",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::Vertex,
            },
            {
                .semantic = "pulled_mesh_indices",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::Vertex,
            },
            {
                .semantic = "scalar_field_texture",
                .kind = RenderBindingKind::TextureSrv,
                .visibility = ShaderVisibility::Vertex,
            },
        };
        layout.samplers = {
            {
                .kind = StaticSamplerKind::LinearClamp,
                .visibility = ShaderVisibility::Vertex,
            },
        };
        return layout;
    }

}

TEST(RenderBindingLayoutAssetModule, CreateRejectsEmptyName)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(), "wozzits_rbl_reject_name_tests");
    wz::fs::create_directories(root);

    EngineAssetLibrary assets(device, logger, root);

    const auto asset =
        assets.render_binding_layouts().create_render_binding_layout({
            .name = "",
            .layout = clipmap_layout(),
        });

    EXPECT_FALSE(asset.valid());
}

TEST(RenderBindingLayoutAssetModule, CreateRejectsInvalidLayout)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(), "wozzits_rbl_reject_layout_tests");
    wz::fs::create_directories(root);

    EngineAssetLibrary assets(device, logger, root);

    RenderBindingLayoutData layout = clipmap_layout();
    layout.bindings.push_back(layout.bindings[0]);  // duplicate semantic

    const auto asset =
        assets.render_binding_layouts().create_render_binding_layout({
            .name = "layout/dup_semantic",
            .layout = layout,
        });

    EXPECT_FALSE(asset.valid());
}

TEST(RenderBindingLayoutAssetModule, CompileRoundTrip)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(), "wozzits_rbl_roundtrip_tests");
    wz::fs::create_directories(root);

    EngineAssetLibrary assets(device, logger, root);

    RenderBindingLayoutData layout = clipmap_layout();
    layout.constant_fields = {
        { .name = "tint", .type = RenderBindingConstantType::Color },
        { .name = "strength", .type = RenderBindingConstantType::Float },
    };
    layout.constants_dwords = 40;  // 32 head + 5 tail + padding

    const auto asset =
        assets.render_binding_layouts().create_render_binding_layout({
            .name = "layout/clipmap_roundtrip",
            .layout = layout,
        });
    ASSERT_TRUE(asset.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle =
        assets.render_binding_layouts().get_render_binding_layout(asset);
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeRenderBindingLayout);

    const RenderBindingLayoutData* data =
        assets.render_binding_layouts().get_render_binding_layout_data(
            handle);
    ASSERT_NE(data, nullptr);

    EXPECT_EQ(data->constants_semantic, "clipmap");
    EXPECT_EQ(data->constants_visibility, ShaderVisibility::All);
    EXPECT_EQ(data->constants_head, RenderBindingConstantsHead::CameraSnappedTerrain);
    EXPECT_EQ(data->constants_dwords, 40u);
    EXPECT_EQ(data->total_constants_dwords(), 40u);
    ASSERT_EQ(data->constant_fields.size(), 2u);
    EXPECT_EQ(data->constant_fields[0].name, "tint");
    EXPECT_EQ(data->constant_fields[0].type, RenderBindingConstantType::Color);
    EXPECT_EQ(data->constant_fields[1].name, "strength");
    EXPECT_EQ(data->constant_fields[1].type, RenderBindingConstantType::Float);
    ASSERT_EQ(data->bindings.size(), 3u);
    EXPECT_EQ(data->bindings[0].semantic, "pulled_mesh_positions");
    EXPECT_EQ(data->bindings[2].kind, RenderBindingKind::TextureSrv);
    ASSERT_EQ(data->samplers.size(), 1u);
    EXPECT_EQ(data->samplers[0].kind, StaticSamplerKind::LinearClamp);
    EXPECT_EQ(data->samplers[0].visibility, ShaderVisibility::Vertex);
}

TEST(RenderBindingLayout, SrgDerivesRegistersFromRowOrder)
{
    RenderBindingLayoutSrg srg{};
    std::string error;
    ASSERT_TRUE(build_render_binding_layout_srg(clipmap_layout(), srg, error))
        << error;

    // One root-constant block: b0 space2, 32 dwords derived from the head.
    ASSERT_EQ(srg.root_constants.size(), 1u);
    EXPECT_EQ(srg.root_constants[0].visibility, ShaderVisibility::All);
    EXPECT_EQ(srg.root_constants[0].shader_register, 0u);
    EXPECT_EQ(srg.root_constants[0].register_space, 2u);
    EXPECT_EQ(srg.root_constants[0].value_count, 32u);
    EXPECT_EQ(srg.root_constants[0].semantic, "clipmap");

    // t0/t1/t2 space2 in row order.
    ASSERT_EQ(srg.descriptor_bindings.size(), 3u);
    for (uint32_t i = 0; i < 3u; ++i) {
        EXPECT_EQ(srg.descriptor_bindings[i].shader_register, i);
        EXPECT_EQ(srg.descriptor_bindings[i].register_space, 2u);
        EXPECT_EQ(srg.descriptor_bindings[i].descriptor_count, 1u);
    }
    EXPECT_EQ(
        srg.descriptor_bindings[0].kind,
        DescriptorKind::StructuredBufferSRV);
    EXPECT_EQ(
        srg.descriptor_bindings[0].semantic,
        DescriptorSemantic::PulledMeshPositions);
    EXPECT_EQ(
        srg.descriptor_bindings[1].semantic,
        DescriptorSemantic::PulledMeshIndices);
    EXPECT_EQ(srg.descriptor_bindings[2].kind, DescriptorKind::TextureSRV);
    EXPECT_EQ(
        srg.descriptor_bindings[2].semantic,
        DescriptorSemantic::ScalarFieldTexture);

    // s0 space2.
    ASSERT_EQ(srg.static_samplers.size(), 1u);
    EXPECT_EQ(srg.static_samplers[0].kind, StaticSamplerKind::LinearClamp);
    EXPECT_EQ(srg.static_samplers[0].visibility, ShaderVisibility::Vertex);
    EXPECT_EQ(srg.static_samplers[0].shader_register, 0u);
    EXPECT_EQ(srg.static_samplers[0].register_space, 2u);
}

TEST(RenderBindingLayout, SrgRejectsUnknownSemantic)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.bindings[1].semantic = "no_such_semantic";

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
    EXPECT_NE(error.find("no_such_semantic"), std::string::npos);
}

TEST(RenderBindingLayout, SrgRejectsUnknownIsNotAuthorable)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.bindings[1].semantic = "unknown";

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
}

TEST(RenderBindingLayout, SrgRejectsUndersizedConstantBlock)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.constants_dwords = 16;  // < 32-dword head

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
}

TEST(RenderBindingLayout, SrgRejectsConstantsWithoutSemantic)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.constants_semantic.clear();

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
}

TEST(RenderBindingLayout, SrgRejectsDuplicateFieldNames)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.constant_fields = {
        { .name = "tint", .type = RenderBindingConstantType::Color },
        { .name = "tint", .type = RenderBindingConstantType::Float },
    };

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
}

TEST(RenderBindingLayout, DerivedDwordsAreHeadPlusTail)
{
    RenderBindingLayoutData layout{};
    layout.constants_semantic = "mesh_style";
    layout.constants_head = RenderBindingConstantsHead::Mvp16;
    layout.constant_fields = {
        { .name = "wireframe_color", .type = RenderBindingConstantType::Color },
        { .name = "surface_color", .type = RenderBindingConstantType::Color },
        { .name = "params", .type = RenderBindingConstantType::Float4 },
    };

    // The preset-4 mesh_style block: mvp16 head + 12 dwords of style tail.
    EXPECT_EQ(layout.tail_dwords(), 12u);
    EXPECT_EQ(layout.total_constants_dwords(), 28u);
    EXPECT_TRUE(layout.valid());
}

TEST(RenderBindingLayout, KeyChangesWithEachSection)
{
    const RenderBindingLayoutData base = clipmap_layout();
    const wz::asset::AssetKey base_key =
        make_render_binding_layout_key("layout/key", base);

    {
        RenderBindingLayoutData changed = base;
        changed.constants_semantic = "other";
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/key", changed));
    }
    {
        RenderBindingLayoutData changed = base;
        changed.constants_visibility = ShaderVisibility::Vertex;
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/key", changed));
    }
    {
        RenderBindingLayoutData changed = base;
        changed.constant_fields.push_back(
            { .name = "tint", .type = RenderBindingConstantType::Color });
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/key", changed));
    }
    {
        RenderBindingLayoutData changed = base;
        changed.bindings[0].visibility = ShaderVisibility::All;
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/key", changed));
    }
    {
        RenderBindingLayoutData changed = base;
        changed.samplers[0].kind = StaticSamplerKind::LinearWrap;
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/key", changed));
    }
    {
        // Same content, same name -> same key (content addressing).
        EXPECT_TRUE(
            base_key == make_render_binding_layout_key("layout/key", base));
        EXPECT_FALSE(
            base_key == make_render_binding_layout_key("layout/other", base));
    }
}

// ── HLSL cbuffer packing (issue #231) ────────────────────────────────────────
//
// The block is read by the shader as a cbuffer, so field offsets follow
// cbuffer packing (a vector may not straddle a 16-byte register), NOT a tight
// dword sum. render_binding_constant_field_offset is the ONE rule the recipe
// baking, the tail_dwords derivation, and the generated prelude all share.

TEST(RenderBindingConstantPacking, FieldOffsetRespectsNoStraddleRule)
{
    using T = RenderBindingConstantType;

    // Scalars pack tightly into any lane.
    EXPECT_EQ(render_binding_constant_field_offset(0, T::Float), 0u);
    EXPECT_EQ(render_binding_constant_field_offset(1, T::Float), 1u);
    EXPECT_EQ(render_binding_constant_field_offset(3, T::Float), 3u);

    // A vector that fits before the boundary stays put …
    EXPECT_EQ(render_binding_constant_field_offset(0, T::Float4), 0u);
    EXPECT_EQ(render_binding_constant_field_offset(2, T::Float2), 2u);
    EXPECT_EQ(render_binding_constant_field_offset(1, T::Float3), 1u);

    // … one that would cross the 16-byte boundary jumps to the next register.
    EXPECT_EQ(render_binding_constant_field_offset(1, T::Float4), 4u);
    EXPECT_EQ(render_binding_constant_field_offset(3, T::Float2), 4u);
    EXPECT_EQ(render_binding_constant_field_offset(2, T::Float3), 4u);
}

TEST(RenderBindingConstantPacking, TailDwordsGrowsWithAlignmentPadding)
{
    // float (1) then float4: the float4 can't start at lane 1, so it pads to
    // lane 4 — the tail spans 8 dwords, not the tight sum of 5.
    RenderBindingLayoutData layout{};
    layout.constants_semantic = "packed";
    layout.constants_head = RenderBindingConstantsHead::None;
    layout.constant_fields = {
        { .name = "amount", .type = RenderBindingConstantType::Float },
        { .name = "tint", .type = RenderBindingConstantType::Float4 },
    };

    EXPECT_EQ(layout.tail_dwords(), 8u);
    EXPECT_EQ(layout.total_constants_dwords(), 8u);
    EXPECT_TRUE(layout.valid());
}

// ── Generated HLSL binding prelude (issue #231) ──────────────────────────────

TEST(RenderBindingPrelude, EmitsCbufferSrvsAndSamplerFromLayout)
{
    // clipmap_layout() + a Mvp16 head and a float4 tail so the cbuffer has
    // both a head packer and a declared field.
    RenderBindingLayoutData layout = clipmap_layout();
    layout.constants_head = RenderBindingConstantsHead::Mvp16;
    layout.constants_dwords = 0;
    layout.constant_fields = {
        { .name = "tint", .type = RenderBindingConstantType::Float4 },
    };

    std::string prelude;
    std::string error;
    ASSERT_TRUE(generate_hlsl_binding_prelude(layout, prelude, error)) << error;

    EXPECT_NE(prelude.find("// Generated binding prelude"), std::string::npos);
    EXPECT_NE(
        prelude.find("cbuffer clipmap : register(b0, space2)"),
        std::string::npos);
    // Head packer at c0 (a float4x4 spans c0..c3), the declared tail float4
    // at c4 (dword 16).
    EXPECT_NE(
        prelude.find("float4x4 mvp : packoffset(c0);"), std::string::npos);
    EXPECT_NE(
        prelude.find("float4 tint : packoffset(c4);"), std::string::npos);
    // SRV rows in derived register order, structured with canonical element
    // types and the texture as Texture2D<float4>.
    EXPECT_NE(
        prelude.find(
            "StructuredBuffer<float3> pulled_mesh_positions : "
            "register(t0, space2);"),
        std::string::npos);
    EXPECT_NE(
        prelude.find(
            "StructuredBuffer<uint> pulled_mesh_indices : "
            "register(t1, space2);"),
        std::string::npos);
    EXPECT_NE(
        prelude.find(
            "Texture2D<float4> scalar_field_texture : register(t2, space2);"),
        std::string::npos);
    // Static sampler, row-derived register.
    EXPECT_NE(
        prelude.find("SamplerState sampler0 : register(s0, space2);"),
        std::string::npos);
}

TEST(RenderBindingPrelude, StraddlingTailFieldGetsPaddedPackoffset)
{
    RenderBindingLayoutData layout{};
    layout.constants_semantic = "packed";
    layout.constants_head = RenderBindingConstantsHead::None;
    layout.constant_fields = {
        { .name = "amount", .type = RenderBindingConstantType::Float },
        { .name = "tint", .type = RenderBindingConstantType::Float4 },
    };

    std::string prelude;
    std::string error;
    ASSERT_TRUE(generate_hlsl_binding_prelude(layout, prelude, error)) << error;

    // amount at c0.x, tint padded to the next register (c1.x = dword 4), never
    // c0.y — that would straddle. The packoffset the shader reads MUST equal
    // the offset the recipe bakes (render_binding_constant_field_offset).
    EXPECT_NE(prelude.find("float amount : packoffset(c0);"), std::string::npos);
    EXPECT_NE(prelude.find("float4 tint : packoffset(c1);"), std::string::npos);
}

TEST(RenderBindingPrelude, EmitsSplatStructAndBufferForSplatCloudRow)
{
    RenderBindingLayoutData layout{};
    layout.bindings = {
        {
            .semantic = "splat_cloud",
            .kind = RenderBindingKind::StructuredSrv,
            .visibility = ShaderVisibility::Vertex,
        },
    };

    std::string prelude;
    std::string error;
    ASSERT_TRUE(generate_hlsl_binding_prelude(layout, prelude, error)) << error;

    EXPECT_NE(prelude.find("struct WzSplat"), std::string::npos);
    EXPECT_NE(
        prelude.find(
            "StructuredBuffer<WzSplat> splat_cloud : register(t0, space2);"),
        std::string::npos);
}

TEST(RenderBindingPrelude, EmitsRegisterMacroWhenNoCanonicalElementType)
{
    // mesh_mask_rules is a valid StructuredSrv semantic with no canonical
    // shader element type — the layout still owns the register, so the prelude
    // hands the author a register macro instead of guessing an element.
    RenderBindingLayoutData layout{};
    layout.bindings = {
        {
            .semantic = "mesh_mask_rules",
            .kind = RenderBindingKind::StructuredSrv,
            .visibility = ShaderVisibility::Pixel,
        },
    };

    std::string prelude;
    std::string error;
    ASSERT_TRUE(generate_hlsl_binding_prelude(layout, prelude, error)) << error;

    EXPECT_NE(
        prelude.find("#define WZ_BINDING_MESH_MASK_RULES register(t0, space2)"),
        std::string::npos);
    EXPECT_EQ(prelude.find("StructuredBuffer<"), std::string::npos)
        << "guessed an element type for a semantic with no canonical one";
}

TEST(RenderBindingPrelude, FailsOnInvalidLayoutWithReason)
{
    RenderBindingLayoutData layout = clipmap_layout();
    layout.bindings[1].semantic = "no_such_semantic";

    std::string prelude;
    std::string error;
    EXPECT_FALSE(generate_hlsl_binding_prelude(layout, prelude, error));
    EXPECT_NE(error.find("no_such_semantic"), std::string::npos);
    EXPECT_TRUE(prelude.empty());
}

TEST(RenderBindingPrelude, EmitsStandardHelperLibrary)
{
    // The standard-helper block is layout-independent: it must appear in every
    // generated prelude so custom programs can consume common projections via a
    // one-liner. A minimal (empty) layout still carries it.
    RenderBindingLayoutData layout{};

    std::string prelude;
    std::string error;
    ASSERT_TRUE(generate_hlsl_binding_prelude(layout, prelude, error)) << error;

    EXPECT_NE(
        prelude.find("// --- wozzits standard helpers ---"), std::string::npos);
    EXPECT_NE(
        prelude.find(
            "float3 wz_origin_relative_direction(float3 world_pos, "
            "float3 origin)"),
        std::string::npos);
    EXPECT_NE(
        prelude.find("return normalize(world_pos - origin);"),
        std::string::npos);
}

TEST(RenderBindingPrelude, CameraSnappedTerrainHeadEmitsNamedTerrainMembers)
{
    // clipmap_layout() carries the CameraSnappedTerrain head (issue #233): the
    // prelude must emit the real named terrain cbuffer members matching the
    // ClipmapDrawConstants byte layout, not an opaque array.
    std::string prelude;
    std::string error;
    ASSERT_TRUE(generate_hlsl_binding_prelude(clipmap_layout(), prelude, error))
        << error;

    EXPECT_NE(
        prelude.find("float4x4 view_projection : packoffset(c0);"),
        std::string::npos);
    EXPECT_NE(
        prelude.find("float4 snap_params : packoffset(c4);"),
        std::string::npos);
    EXPECT_NE(
        prelude.find("float4 world_to_uv : packoffset(c5);"),
        std::string::npos);
    EXPECT_NE(
        prelude.find("float4 texel_and_vertical : packoffset(c6);"),
        std::string::npos);
    EXPECT_NE(
        prelude.find("float4 texel_dims_extent : packoffset(c7);"),
        std::string::npos);
    // The height field the VS samples still comes through as an SRV row.
    EXPECT_NE(
        prelude.find(
            "Texture2D<float4> scalar_field_texture : register(t2, space2);"),
        std::string::npos);
}

// ── View-frequency block (register space 0) ──────────────────────────────────
// Frame state every program sees identically -- the observer, and the
// atmosphere it looks through -- as opposed to the object block, which
// describes one draw.

// The safety property. This touches the derivation every authored program goes
// through, so a layout that declares no view block must produce exactly the
// rows it produced before the block existed: one root-constant binding, in the
// object space, and nothing at space 0.
TEST(RenderBindingLayout, LayoutWithoutViewBlockIsUnchanged)
{
    using namespace wz::engine::assets;

    const RenderBindingLayoutData layout = clipmap_layout();
    ASSERT_FALSE(layout.has_view_constants());

    RenderBindingLayoutSrg srg{};
    std::string error;
    ASSERT_TRUE(build_render_binding_layout_srg(layout, srg, error)) << error;

    ASSERT_EQ(srg.root_constants.size(), 1u);
    EXPECT_EQ(srg.root_constants[0].register_space,
              kRenderBindingLayoutRegisterSpace);
    for (const auto& binding : srg.descriptor_bindings) {
        EXPECT_EQ(binding.register_space, kRenderBindingLayoutRegisterSpace);
    }
    for (const auto& sampler : srg.static_samplers) {
        EXPECT_EQ(sampler.register_space, kRenderBindingLayoutRegisterSpace);
    }
}

// With one declared, the derivation gains a SECOND root-constant binding at
// space 0. The rhi bridge groups by register space, so this is what becomes a
// binding_slot 0 group -- no bridge change needed for that to happen.
TEST(RenderBindingLayout, ViewBlockDerivesRootConstantsAtSpaceZero)
{
    using namespace wz::engine::assets;

    RenderBindingLayoutData layout = clipmap_layout();
    layout.view_constants_semantic = "wz_view";
    layout.view_head = RenderBindingViewHead::CameraFog;

    RenderBindingLayoutSrg srg{};
    std::string error;
    ASSERT_TRUE(build_render_binding_layout_srg(layout, srg, error)) << error;

    ASSERT_EQ(srg.root_constants.size(), 2u);

    const auto* object = &srg.root_constants[0];
    const auto* view = &srg.root_constants[1];
    if (object->register_space == kRenderBindingLayoutViewRegisterSpace) {
        std::swap(object, view);
    }

    EXPECT_EQ(object->register_space, kRenderBindingLayoutRegisterSpace);
    EXPECT_EQ(object->semantic, "clipmap");

    EXPECT_EQ(view->register_space, kRenderBindingLayoutViewRegisterSpace);
    EXPECT_EQ(view->semantic, "wz_view");
    // 12 dwords: camera_world_position, fog colour + density, fog params.
    EXPECT_EQ(view->value_count, 12u);
    EXPECT_EQ(view->shader_register, 0u);

    // Both blocks sit at b0 in their OWN space, which is the whole point of
    // separating them by frequency.
    EXPECT_EQ(object->shader_register, view->shader_register);
    EXPECT_NE(object->register_space, view->register_space);
}

// A head with no semantic would silently vanish from the derived SRG -- the
// same contradiction the object block already rejects.
TEST(RenderBindingLayout, SrgRejectsViewHeadWithoutSemantic)
{
    using namespace wz::engine::assets;

    RenderBindingLayoutData layout = clipmap_layout();
    layout.view_head = RenderBindingViewHead::CameraFog;

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(layout.valid());
}

// And the reverse: a named block of zero dwords would emit an empty cbuffer
// and a root parameter nothing ever fills.
TEST(RenderBindingLayout, SrgRejectsViewSemanticWithoutHead)
{
    using namespace wz::engine::assets;

    RenderBindingLayoutData layout = clipmap_layout();
    layout.view_constants_semantic = "wz_view";

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
    EXPECT_FALSE(layout.valid());
}

// Two cbuffers of the same name will not compile, and the collision is silent
// until the program is built -- much later, and much harder to read.
TEST(RenderBindingLayout, SrgRejectsViewAndObjectSharingASemanticName)
{
    using namespace wz::engine::assets;

    RenderBindingLayoutData layout = clipmap_layout();
    layout.view_constants_semantic = layout.constants_semantic;
    layout.view_head = RenderBindingViewHead::CameraFog;

    RenderBindingLayoutSrg srg{};
    std::string error;
    EXPECT_FALSE(build_render_binding_layout_srg(layout, srg, error));
    EXPECT_NE(error.find("share a semantic"), std::string::npos) << error;
}

// ── The generated prelude must actually COMPILE ──────────────────────────────
// The prelude is generated text prepended to every authored shader, so a syntax
// error in the generator breaks every program at once -- and a golden-text test
// would not catch it, because the text can be exactly what was intended and
// still not be valid HLSL. These compile it for real.
//
// No device needed: D3DCompile is the compiler library, not the runtime.

namespace
{
    bool compile_hlsl_source(const std::string& source,
                             const char* entry,
                             const char* profile,
                             std::string& error)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> code;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(
            source.c_str(), source.size(), nullptr, nullptr, nullptr,
            entry, profile, 0, 0, code.GetAddressOf(), errors.GetAddressOf());
        if (SUCCEEDED(hr) && code && code->GetBufferSize() > 0u) {
            return true;
        }
        error = errors
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "no diagnostic";
        return false;
    }
}

TEST(RenderBindingPrelude, ViewBlockPreludeCompilesAndFogsAOneLiner)
{
    using namespace wz::engine::assets;

    RenderBindingLayoutData layout = clipmap_layout();
    layout.view_constants_semantic = "wz_view";
    layout.view_head = RenderBindingViewHead::CameraFog;

    std::string prelude;
    std::string error;
    ASSERT_TRUE(generate_hlsl_binding_prelude(layout, prelude, error)) << error;

    // The one-liner a shader is meant to write, against the real prelude.
    const std::string source = prelude +
        "\nfloat4 main(float3 world_pos : TEXCOORD0) : SV_TARGET\n"
        "{\n"
        "    float3 lit = float3(0.4f, 0.5f, 0.3f);\n"
        "    return float4(wz_fog_from_view(lit, world_pos), 1.0f);\n"
        "}\n";

    ASSERT_TRUE(compile_hlsl_source(source, "main", "ps_5_1", error))
        << "generated view-block prelude does not compile:\n" << error;
}

// The standard helper section is documented as pure functions with no bindings,
// so that it compiles against a layout declaring nothing. wz_apply_fog has to
// hold that line -- if it reached for the view cbuffer instead of taking its
// terms as arguments, every layout without a view block would stop compiling.
TEST(RenderBindingPrelude, StandardHelpersCompileWithoutAnyViewBlock)
{
    using namespace wz::engine::assets;

    RenderBindingLayoutData layout{};   // declares nothing at all
    std::string prelude;
    std::string error;
    ASSERT_TRUE(generate_hlsl_binding_prelude(layout, prelude, error)) << error;

    EXPECT_EQ(prelude.find("wz_fog_from_view"), std::string::npos)
        << "the binding-aware wrapper must not be emitted without a view block";

    const std::string source = prelude +
        "\nfloat4 main() : SV_TARGET\n"
        "{\n"
        "    float3 c = wz_apply_fog(float3(1, 0, 0), float3(0.5f, 0.6f, 0.7f),\n"
        "                            0.001f, 100.0f, 2000.0f);\n"
        "    return float4(c + wz_origin_relative_direction(\n"
        "        float3(1, 2, 3), float3(0, 0, 0)), 1.0f);\n"
        "}\n";

    ASSERT_TRUE(compile_hlsl_source(source, "main", "ps_5_1", error))
        << "bindings-free helper section does not compile standalone:\n" << error;
}
