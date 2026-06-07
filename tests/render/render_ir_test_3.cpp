#include <gtest/gtest.h>
#include <render/ir/render_ir.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>

using namespace wz::render;
using namespace wz::scene;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

    Mat4 translation_z(float z)
    {
        Mat4 m = Mat4::identity();
        m.m[14] = z;
        return m;
    }

    AABB unit_box()
    {
        return { Vec3{-0.5f,-0.5f,-0.5f}, Vec3{0.5f,0.5f,0.5f} };
    }

    wz::asset::AssetKey test_proxy_key()
    {
        return wz::asset::AssetKey{
            .content_hash = { 1, 2 },
            .schema_hash = { 3, 4 },
            .compiler_hash = { 5, 6 },
            .deps_hash = { 7, 8 },
        };
    }

    wz::engine::assets::TerrainVisualProxyData test_proxy_data()
    {
        const wz::asset::AssetKey proxy_key = test_proxy_key();
        wz::engine::assets::TerrainVisualProxyData proxy{};
        proxy.compiler_version = 1u;
        proxy.source_asset_key = proxy_key;
        proxy.terrain_proxy_id =
            wz::engine::assets::TerrainProxyId{ proxy_key };
        proxy.bounds.min[0] = -1.0f;
        proxy.bounds.min[1] = -1.0f;
        proxy.bounds.min[2] = -1.0f;
        proxy.bounds.max[0] = 1.0f;
        proxy.bounds.max[1] = 1.0f;
        proxy.bounds.max[2] = 1.0f;
        for (uint32_t i = 0u; i < 2u; ++i) {
            wz::engine::assets::TerrainVisualProxyChunkRecord chunk{};
            chunk.chunk_id = wz::engine::assets::TerrainChunkId{ i };
            chunk.representation_id =
                wz::engine::assets::TerrainRepresentationId{ i };
            chunk.bounds.min[0] = i == 0u ? -1.0f : 0.0f;
            chunk.bounds.min[1] = -1.0f;
            chunk.bounds.min[2] = -1.0f;
            chunk.bounds.max[0] = i == 0u ? 0.0f : 1.0f;
            chunk.bounds.max[1] = 1.0f;
            chunk.bounds.max[2] = 1.0f;
            chunk.triangle_count = 2u;
            chunk.vertex_count = 4u;
            wz::engine::assets::TerrainVisualProxyLodRecord lod{};
            lod.lod_id = wz::engine::assets::TerrainLodId{ 0u };
            lod.representation_id = chunk.representation_id;
            lod.bounds = chunk.bounds;
            lod.triangle_count = 2u;
            lod.vertex_count = 4u;
            chunk.lods.push_back(lod);
            proxy.chunks.push_back(std::move(chunk));
        }
        return proxy;
    }

    wz::engine::assets::TerrainVisualProxyData test_transition_proxy_data()
    {
        wz::engine::assets::TerrainVisualProxyData proxy = test_proxy_data();
        for (auto& chunk : proxy.chunks) {
            wz::engine::assets::TerrainVisualProxyLodRecord lod =
                chunk.lods.front();
            lod.lod_id = wz::engine::assets::TerrainLodId{ 1u };
            lod.triangle_count = 1u;
            lod.index_count = 3u;
            lod.conservative_geometric_error = 1.0f;
            chunk.lods.push_back(lod);
        }

        wz::engine::assets::TerrainVisualProxyTransitionStrip strip{};
        strip.chunk_id = wz::engine::assets::TerrainChunkId{ 0u };
        strip.neighbor_chunk_id = wz::engine::assets::TerrainChunkId{ 1u };
        strip.lod_id = wz::engine::assets::TerrainLodId{ 0u };
        strip.neighbor_lod_id = wz::engine::assets::TerrainLodId{ 1u };
        strip.edge =
            wz::engine::assets::TerrainVisualProxyBoundaryEdge::PositiveX;
        strip.vertices.resize(4u);
        strip.vertices[0].position[0] = 0.0f;
        strip.vertices[0].position[2] = -1.0f;
        strip.vertices[0].side = 0u;
        strip.vertices[1].position[0] = 0.0f;
        strip.vertices[1].position[2] = -1.0f;
        strip.vertices[1].side = 1u;
        strip.vertices[2].position[0] = 0.0f;
        strip.vertices[2].position[2] = 1.0f;
        strip.vertices[2].side = 0u;
        strip.vertices[3].position[0] = 0.0f;
        strip.vertices[3].position[2] = 1.0f;
        strip.vertices[3].side = 1u;
        strip.indices = { 0u, 1u, 2u, 2u, 1u, 3u };
        proxy.chunks[0].transition_strips.push_back(std::move(strip));
        return proxy;
    }

    ViewData camera_at_z(float z)
    {
        ViewData v{};
        v.view = Mat4::identity();
        v.view.m[14] = -z;
        v.projection = Mat4::identity();
        // Wide orthographic VP: frustum ≈ ±10000 so test objects at z=1–10 are never culled.
        // Depth computation uses v.view, not view_projection.
        v.view_projection.m[0]  = 1e-4f;
        v.view_projection.m[5]  = 1e-4f;
        v.view_projection.m[10] = 1e-4f;
        v.view_projection.m[15] = 1.0f;
        v.camera_position = Vec3{ 0.f, 0.f, z };
        return v;
    }

    // Build a compiled scene with:
    //   3 opaque nodes  — materials 2, 0, 1 (deliberately unordered)
    //   3 transparent   — depths 1, 3, 2    (deliberately unordered)
    //   3 splats        — at z=5, z=1, z=3  (deliberately unordered)
    //   2 particles     — at z=2, z=4

    CompiledSceneStorage make_compiled_scene(ViewData view = camera_at_z(0.f))
    {
        SceneBuilder b;

        auto make_node = [](float z) {
            TransformNode n{};
            n.local = translation_z(z);
            n.flags = TransformNodeFlag::RenderDomain;
            return n;
            };

        auto root = add_node(b, [] { TransformNode n{}; return n; }());

        // Opaque nodes — materials 2, 0, 1
        auto op0 = add_node(b, make_node(1.f));
        auto op1 = add_node(b, make_node(2.f));
        auto op2 = add_node(b, make_node(3.f));

        // Transparent nodes — z positions give depths when camera at z=0
        auto tr0 = add_node(b, make_node(1.f));
        auto tr1 = add_node(b, make_node(3.f));
        auto tr2 = add_node(b, make_node(2.f));

        // Splat nodes
        auto sp0 = add_node(b, make_node(5.f));
        auto sp1 = add_node(b, make_node(1.f));
        auto sp2 = add_node(b, make_node(3.f));

        // Particle nodes
        auto pa0 = add_node(b, make_node(2.f));
        auto pa1 = add_node(b, make_node(4.f));

        for (auto n : { op0,op1,op2,tr0,tr1,tr2,sp0,sp1,sp2,pa0,pa1 })
            add_edge(b, root, n);

        auto storage = build(std::move(b));
        assert(storage.has_value());
        propagate_all(storage->polytree);

        std::vector<RenderableDescriptor> descs(12);
        descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
        descs[op0] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      2u, 2u, unit_box() };
        descs[op1] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      0u, 0u, unit_box() };
        descs[op2] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      1u, 1u, unit_box() };
        descs[tr0] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 0u, 0u, unit_box() };
        descs[tr1] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 1u, 1u, unit_box() };
        descs[tr2] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 2u, 2u, unit_box() };
        descs[sp0] = { classify_legacy_renderable(RenderPipeline::Splat), INVALID_MESH, INVALID_MATERIAL,
                        {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f} };
        descs[sp1] = { classify_legacy_renderable(RenderPipeline::Splat), INVALID_MESH, INVALID_MATERIAL,
                        {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f} };
        descs[sp2] = { classify_legacy_renderable(RenderPipeline::Splat), INVALID_MESH, INVALID_MATERIAL,
                        {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f} };
        descs[pa0] = { classify_legacy_renderable(RenderPipeline::Particle), 0u, 0u, unit_box() };
        descs[pa1] = { classify_legacy_renderable(RenderPipeline::Particle), 1u, 1u, unit_box() };

        CompiledSceneStorage cs{};
        compile(cs, storage->polytree, descs, {}, view);
        return cs;
    }

    CompiledSceneStorage make_terrain_scene(ViewData view = camera_at_z(0.f))
    {
        SceneBuilder b;
        TransformNode root{};
        NodeHandle root_h = add_node(b, root);

        TransformNode terrain_node{};
        terrain_node.local = translation_z(1.0f);
        terrain_node.flags = TransformNodeFlag::RenderDomain;
        NodeHandle terrain_h = add_node(b, terrain_node);
        add_edge(b, root_h, terrain_h);

        auto storage = build(std::move(b));
        assert(storage.has_value());
        propagate_all(storage->polytree);

        const wz::asset::AssetKey proxy_key = test_proxy_key();
        static const wz::engine::assets::TerrainVisualProxyData proxy =
            test_proxy_data();
        std::vector<RenderableDescriptor> descs(node_count(storage->polytree));
        descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
        descs[terrain_h] = RenderableDescriptor{
            .node_class = SceneNodeClass{
                .role = SceneRole::Renderable,
                .producer = ProducerKind::TerrainPatch,
                .default_surface = SurfaceClass::Opaque,
                .spatial = SpatialKind::Box,
                .compile = CompileBehavior::Static,
                .domains = static_cast<RenderDomainMask>(RenderDomain::Surface),
            },
            .material = 9u,
            .local_bounds = unit_box(),
            .terrain_visual_proxy_asset = proxy_key,
            .terrain_proxy_id =
                wz::engine::assets::TerrainProxyId{ proxy_key },
            .terrain_visual_proxy_data = &proxy,
            .terrain_visual_chunk_count = 2u,
            .visible = true,
        };

        CompiledSceneStorage cs{};
        compile(cs, storage->polytree, descs, {}, view);
        return cs;
    }

    CompiledSceneStorage make_transition_terrain_scene(
        ViewData view = camera_at_z(0.f))
    {
        SceneBuilder b;
        TransformNode root{};
        NodeHandle root_h = add_node(b, root);

        TransformNode terrain_node{};
        terrain_node.local = translation_z(1.0f);
        terrain_node.flags = TransformNodeFlag::RenderDomain;
        NodeHandle terrain_h = add_node(b, terrain_node);
        add_edge(b, root_h, terrain_h);

        auto storage = build(std::move(b));
        assert(storage.has_value());
        propagate_all(storage->polytree);

        const wz::asset::AssetKey proxy_key = test_proxy_key();
        static const wz::engine::assets::TerrainVisualProxyData proxy =
            test_transition_proxy_data();
        std::vector<RenderableDescriptor> descs(node_count(storage->polytree));
        descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
        descs[terrain_h] = RenderableDescriptor{
            .node_class = SceneNodeClass{
                .role = SceneRole::Renderable,
                .producer = ProducerKind::TerrainPatch,
                .default_surface = SurfaceClass::Opaque,
                .spatial = SpatialKind::Box,
                .compile = CompileBehavior::Static,
                .domains = static_cast<RenderDomainMask>(RenderDomain::Surface),
            },
            .material = 9u,
            .local_bounds = unit_box(),
            .terrain_visual_proxy_asset = proxy_key,
            .terrain_proxy_id =
                wz::engine::assets::TerrainProxyId{ proxy_key },
            .terrain_visual_proxy_data = &proxy,
            .terrain_visual_chunk_count = 2u,
            .visible = true,
        };

        CompiledSceneStorage cs{};
        compile(cs, storage->polytree, descs, {}, view);
        return cs;
    }

} // namespace


// ─── DrawRef counts ───────────────────────────────────────────────────────────

TEST(RenderIRSpec, OpaqueRefCountMatchesPrimitives)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);
    EXPECT_EQ(ir.opaque.size(), cs.scene.opaque.size());
}

TEST(RenderIRSpec, TransparentRefCountMatchesPrimitives)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);
    EXPECT_EQ(ir.transparent.size(), cs.scene.transparent.size());
}

TEST(RenderIRSpec, SplatRefCountMatchesPrimitives)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);
    EXPECT_EQ(ir.splats.size(), cs.scene.splats.size());
}

TEST(RenderIRSpec, ParticleRefCountMatchesPrimitives)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);
    EXPECT_EQ(ir.particles.size(), cs.scene.particles.size());
}

TEST(RenderIRSpec, TerrainInstanceCompilesAsFirstClassSceneRecord)
{
    auto cs = make_terrain_scene();

    ASSERT_EQ(cs.scene.terrain_instances.size(), 1u);
    EXPECT_EQ(cs.scene.terrain_instances[0].material, 9u);
    EXPECT_EQ(
        cs.scene.terrain_instances[0].visual_proxy_asset,
        test_proxy_key());
    ASSERT_EQ(cs.metadata.terrain_source_nodes.size(), 1u);
    const NodeHandle source = cs.metadata.terrain_source_nodes[0];
    ASSERT_LT(source, cs.metadata.node_to_output.size());
    EXPECT_EQ(
        cs.metadata.node_to_output[source].kind,
        CompiledOutputKind::TerrainVisualInstance);
    EXPECT_EQ(cs.metadata.node_to_output[source].index, 0u);
}

TEST(RenderIRSpec, TerrainDescriptorChunksCompileToDefaultLodChoices)
{
    auto cs = make_terrain_scene();

    ASSERT_EQ(cs.scene.terrain_lod_choices.size(), 2u);
    EXPECT_EQ(cs.scene.terrain_lod_choices[0].terrain_instance_index, 0u);
    EXPECT_EQ(cs.scene.terrain_lod_choices[0].chunk_id.value, 0u);
    EXPECT_EQ(cs.scene.terrain_lod_choices[0].lod_id.value, 0u);
    EXPECT_EQ(cs.scene.terrain_lod_choices[1].terrain_instance_index, 0u);
    EXPECT_EQ(cs.scene.terrain_lod_choices[1].chunk_id.value, 1u);
    EXPECT_EQ(cs.scene.terrain_lod_choices[1].lod_id.value, 0u);

    RenderIRStorage ir_storage;
    const RenderIRView ir = build_render_ir(ir_storage, cs.scene);

    EXPECT_EQ(ir.terrain.size(), 2u);
}

TEST(RenderIRSpec, TerrainDrawRefsAreFlatLodChoices)
{
    auto cs = make_terrain_scene();
    std::vector<TerrainLodChoice> choices{
        TerrainLodChoice{
            .terrain_instance_index = 0u,
            .chunk_id = wz::engine::assets::TerrainChunkId{ 7 },
            .representation_kind =
                wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks,
            .lod_id = wz::engine::assets::TerrainLodId{ 1 },
            .projected_error_px = 0.5f,
            .projected_area_px = 128.0f,
            .priority = 2.0f,
        },
        TerrainLodChoice{
            .terrain_instance_index = 0u,
            .chunk_id = wz::engine::assets::TerrainChunkId{ 8 },
            .representation_kind =
                wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks,
            .lod_id = wz::engine::assets::TerrainLodId{ 0 },
            .projected_error_px = 0.1f,
            .projected_area_px = 64.0f,
            .priority = 1.0f,
        },
    };

    CompiledSceneView scene = cs.scene;
    scene.terrain_lod_choices = std::span<const TerrainLodChoice>(choices);

    RenderIRStorage ir_storage;
    const RenderIRView ir = build_render_ir(ir_storage, scene);

    ASSERT_EQ(ir.terrain.size(), 2u);
    EXPECT_EQ(ir.culling.visible_terrain, 2u);
    EXPECT_EQ(ir.culling.culled_terrain, 0u);
    for (const TerrainDrawRef& ref : ir.terrain) {
        EXPECT_EQ(ref.terrain_instance_index, 0u);
        EXPECT_EQ(
            ref.representation_kind,
            wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks);
        EXPECT_NE(ref.sort_key, 0u);
    }
    EXPECT_NE(ir.terrain[0].chunk_id, ir.terrain[1].chunk_id);
}

TEST(RenderIRSpec, TerrainDrawRefsIncludeSelectedMixedLodTransition)
{
    auto cs = make_transition_terrain_scene();
    std::vector<TerrainLodChoice> choices{
        TerrainLodChoice{
            .terrain_instance_index = 0u,
            .chunk_id = wz::engine::assets::TerrainChunkId{ 0u },
            .representation_kind =
                wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks,
            .lod_id = wz::engine::assets::TerrainLodId{ 0u },
        },
        TerrainLodChoice{
            .terrain_instance_index = 0u,
            .chunk_id = wz::engine::assets::TerrainChunkId{ 1u },
            .representation_kind =
                wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks,
            .lod_id = wz::engine::assets::TerrainLodId{ 1u },
        },
    };

    CompiledSceneView scene = cs.scene;
    scene.terrain_lod_choices = std::span<const TerrainLodChoice>(choices);

    RenderIRStorage ir_storage;
    const RenderIRView ir = build_render_ir(ir_storage, scene);

    ASSERT_EQ(ir.terrain.size(), 3u);
    const auto transition = std::find_if(
        ir.terrain.begin(),
        ir.terrain.end(),
        [](const TerrainDrawRef& ref) {
            return ref.kind == TerrainDrawRefKind::LodTransition;
        });
    ASSERT_NE(transition, ir.terrain.end());
    EXPECT_EQ(transition->chunk_id.value, 0u);
    EXPECT_EQ(transition->neighbor_chunk_id.value, 1u);
    EXPECT_EQ(transition->lod_id.value, 0u);
    EXPECT_EQ(transition->neighbor_lod_id.value, 1u);
}

TEST(RenderIRSpec, TerrainDrawRefsSkipTransitionForEqualLods)
{
    auto cs = make_transition_terrain_scene();
    std::vector<TerrainLodChoice> choices{
        TerrainLodChoice{
            .terrain_instance_index = 0u,
            .chunk_id = wz::engine::assets::TerrainChunkId{ 0u },
            .representation_kind =
                wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks,
            .lod_id = wz::engine::assets::TerrainLodId{ 0u },
        },
        TerrainLodChoice{
            .terrain_instance_index = 0u,
            .chunk_id = wz::engine::assets::TerrainChunkId{ 1u },
            .representation_kind =
                wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks,
            .lod_id = wz::engine::assets::TerrainLodId{ 0u },
        },
    };

    CompiledSceneView scene = cs.scene;
    scene.terrain_lod_choices = std::span<const TerrainLodChoice>(choices);

    RenderIRStorage ir_storage;
    const RenderIRView ir = build_render_ir(ir_storage, scene);

    ASSERT_EQ(ir.terrain.size(), 2u);
    EXPECT_TRUE(std::none_of(
        ir.terrain.begin(),
        ir.terrain.end(),
        [](const TerrainDrawRef& ref) {
            return ref.kind == TerrainDrawRefKind::LodTransition;
        }));
}


// ─── Opaque sort — ascending material key ────────────────────────────────────

TEST(RenderIRSpec, OpaqueSortedByMaterial)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    // Keys must be non-decreasing
    for (uint32_t i = 1; i < ir.opaque.size(); ++i)
        EXPECT_LE(ir.opaque[i - 1].sort_key, ir.opaque[i].sort_key)
        << "opaque sort broken at index " << i;
}

TEST(RenderIRSpec, OpaqueAllIndicesPresent)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    // Every index 0..N-1 appears exactly once
    std::vector<uint32_t> seen(ir.opaque.size(), 0);
    for (auto& ref : ir.opaque) {
        ASSERT_LT(ref.index, seen.size());
        ++seen[ref.index];
    }
    for (auto c : seen) EXPECT_EQ(c, 1u);
}


// ─── Transparent sort — back-to-front ────────────────────────────────────────

TEST(RenderIRSpec, TransparentSortedBackToFront)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    // Keys non-decreasing = depths non-increasing (further first)
    for (uint32_t i = 1; i < ir.transparent.size(); ++i)
        EXPECT_LE(ir.transparent[i - 1].sort_key, ir.transparent[i].sort_key)
        << "transparent sort broken at index " << i;
}

TEST(RenderIRSpec, TransparentFurthestFirst)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    // First ref should point to the deepest (furthest) transparent primitive
    const auto& first = cs.scene.transparent[ir.transparent[0].index];
    for (auto& ref : ir.transparent) {
        const auto& p = cs.scene.transparent[ref.index];
        EXPECT_LE(first.depth, p.depth)
            << "first transparent is not the furthest";
    }
}

TEST(RenderIRSpec, TransparentAllIndicesPresent)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    std::vector<uint32_t> seen(ir.transparent.size(), 0);
    for (auto& ref : ir.transparent) {
        ASSERT_LT(ref.index, seen.size());
        ++seen[ref.index];
    }
    for (auto c : seen) EXPECT_EQ(c, 1u);
}


// ─── Splat sort — back-to-front ───────────────────────────────────────────────

TEST(RenderIRSpec, SplatsSortedBackToFront)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    for (uint32_t i = 1; i < ir.splats.size(); ++i)
        EXPECT_LE(ir.splats[i - 1].sort_key, ir.splats[i].sort_key)
        << "splat sort broken at index " << i;
}

TEST(RenderIRSpec, SplatFurthestFirst)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    // First splat ref should point to deepest splat
    float first_depth = cs.scene.splat_depths[ir.splats[0].index];
    for (auto& ref : ir.splats)
        EXPECT_LE(first_depth, cs.scene.splat_depths[ref.index]);
}

TEST(RenderIRSpec, SplatAllIndicesPresent)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    std::vector<uint32_t> seen(ir.splats.size(), 0);
    for (auto& ref : ir.splats) {
        ASSERT_LT(ref.index, seen.size());
        ++seen[ref.index];
    }
    for (auto c : seen) EXPECT_EQ(c, 1u);
}


// ─── update_render_ir() — re-sort after camera move ──────────────────────────

TEST(RenderIRSpec, UpdateRenderIRResortsSplats)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    build_render_ir(ir_storage, cs.scene);

    // Record order before camera move
    std::vector<uint32_t> before;
    for (auto& ref : ir_storage.ir.splats) before.push_back(ref.index);

    // Move camera so depths change significantly
    update_view(cs, camera_at_z(10.f));
    update_render_ir(ir_storage, cs.scene);

    std::vector<uint32_t> after;
    for (auto& ref : ir_storage.ir.splats) after.push_back(ref.index);

    // Order must have changed — all splats shift relative to new camera
    EXPECT_NE(before, after);
}

TEST(RenderIRSpec, UpdateRenderIRMaintainsSplatCount)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    build_render_ir(ir_storage, cs.scene);

    uint32_t count_before = static_cast<uint32_t>(ir_storage.ir.splats.size());
    update_view(cs, camera_at_z(10.f));
    update_render_ir(ir_storage, cs.scene);
    uint32_t count_after = static_cast<uint32_t>(ir_storage.ir.splats.size());

    EXPECT_EQ(count_before, count_after);
}

TEST(RenderIRSpec, UpdateRenderIRSplatsStillSortedAfterMove)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    build_render_ir(ir_storage, cs.scene);

    update_view(cs, camera_at_z(6.f));
    update_render_ir(ir_storage, cs.scene);

    for (uint32_t i = 1; i < ir_storage.ir.splats.size(); ++i)
        EXPECT_LE(ir_storage.ir.splats[i - 1].sort_key, ir_storage.ir.splats[i].sort_key)
        << "splat sort broken after camera move at index " << i;
}

TEST(RenderIRSpec, UpdateRenderIRTransparentStillSortedAfterMove)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    build_render_ir(ir_storage, cs.scene);

    update_view(cs, camera_at_z(6.f));
    update_render_ir(ir_storage, cs.scene);

    for (uint32_t i = 1; i < ir_storage.ir.transparent.size(); ++i)
        EXPECT_LE(ir_storage.ir.transparent[i - 1].sort_key, ir_storage.ir.transparent[i].sort_key)
        << "transparent sort broken after camera move at index " << i;
}


// ─── Empty pipelines ──────────────────────────────────────────────────────────

TEST(RenderIRSpec, EmptyCompiledSceneProducesEmptyIR)
{
    SceneBuilder b;
    TransformNode root{};
    add_node(b, root);
    auto s = build(std::move(b));
    ASSERT_TRUE(s.has_value());
    propagate_all(s->polytree);

    std::vector<RenderableDescriptor> descs(1);
    descs[0] = { classify_legacy_renderable(RenderPipeline::None) };
    CompiledSceneStorage cs{};
    compile(cs, s->polytree, descs, {}, camera_at_z(0.f));
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    EXPECT_EQ(ir.opaque.size(), 0u);
    EXPECT_EQ(ir.transparent.size(), 0u);
    EXPECT_EQ(ir.splats.size(), 0u);
    EXPECT_EQ(ir.particles.size(), 0u);
}

TEST(RenderIRSpec, SourceSpansPointIntoCompiledSceneStorage)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);
    // source spans must alias the same backing memory as cs.scene
    EXPECT_EQ(ir.source.opaque.data(), cs.scene.opaque.data());
}
