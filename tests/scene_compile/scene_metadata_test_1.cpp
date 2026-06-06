#include <gtest/gtest.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>

using namespace wz::scene;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

    ViewData identity_view()
    {
        ViewData v{};
        v.view            = Mat4::identity();
        v.projection      = Mat4::identity();
        v.view_projection = Mat4::identity();
        return v;
    }

    struct TwoNodeScene
    {
        SceneStorage                      storage{};
        std::vector<RenderableDescriptor> descs{};
        NodeHandle                        root{};
        NodeHandle                        child{};
    };

    // Root (pipeline None) + one opaque child with a valid mesh.
    TwoNodeScene make_root_plus_opaque()
    {
        SceneBuilder b;

        TransformNode root_node{};
        root_node.local = Mat4::identity();
        NodeHandle root_h = add_node(b, root_node);

        TransformNode child_node{};
        child_node.local = Mat4::identity();
        child_node.flags = TransformNodeFlag::RenderDomain;
        NodeHandle child_h = add_node(b, child_node);

        EXPECT_TRUE(add_edge(b, root_h, child_h));
        auto result = build(std::move(b));
        EXPECT_TRUE(result.has_value());

        TwoNodeScene out{};
        out.storage = std::move(*result);
        out.root    = root_h;
        out.child   = child_h;

        out.descs.resize(node_count(out.storage.polytree));
        out.descs[root_h]  = RenderableDescriptor{ .node_class = classify_legacy_renderable(RenderPipeline::None) };
        out.descs[child_h] = RenderableDescriptor{
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh     = 0u,
            .material = 0u,
            .visible  = true,
        };

        propagate_all(out.storage.polytree);
        return out;
    }

} // namespace


// ─── Test 1: Root plus one opaque child ───────────────────────────────────────
//
// root  → node_to_output: None / INVALID
// child → node_to_output: SurfacePrimitive / 0
// opaque_source_nodes[0] == child

TEST(SceneMetadata, RootPlusOneOpaqueChild)
{
    auto scene = make_root_plus_opaque();
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    const CompiledSceneMetadataView& m = storage.metadata;

    ASSERT_EQ(m.node_to_output.size(), node_count(scene.storage.polytree));
    ASSERT_EQ(m.opaque_source_nodes.size(), 1u);

    // Root produced no output
    EXPECT_EQ(m.node_to_output[scene.root].kind,  CompiledOutputKind::None);
    EXPECT_EQ(m.node_to_output[scene.root].index, INVALID_COMPILED_OUTPUT);

    // Child produced the first opaque record
    EXPECT_EQ(m.node_to_output[scene.child].kind,  CompiledOutputKind::SurfacePrimitive);
    EXPECT_EQ(m.node_to_output[scene.child].index, 0u);

    // Reverse mapping
    EXPECT_EQ(m.opaque_source_nodes[0], scene.child);
}


// ─── Test 2: Multiple opaque children ────────────────────────────────────────
//
// Each child gets a unique output index.
// Each opaque_source_nodes[i] maps back to the correct child.

TEST(SceneMetadata, MultipleOpaqueChildren)
{
    SceneBuilder b;

    TransformNode root_node{};
    root_node.local = Mat4::identity();
    NodeHandle root_h = add_node(b, root_node);

    constexpr int N = 3;
    NodeHandle children[N]{};
    for (int i = 0; i < N; ++i) {
        TransformNode cn{};
        cn.local = Mat4::identity();
        cn.flags = TransformNodeFlag::RenderDomain;
        children[i] = add_node(b, cn);
        EXPECT_TRUE(add_edge(b, root_h, children[i]));
    }

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    SceneStorage scene = std::move(*result);

    std::vector<RenderableDescriptor> descs(node_count(scene.polytree));
    descs[root_h] = RenderableDescriptor{ .node_class = classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < N; ++i) {
        descs[children[i]] = RenderableDescriptor{
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh     = static_cast<MeshHandle>(i),
            .material = 0u,
            .visible  = true,
        };
    }
    propagate_all(scene.polytree);

    CompiledSceneStorage storage{};
    compile(storage, scene.polytree, descs, {}, identity_view());

    const CompiledSceneMetadataView& m = storage.metadata;
    ASSERT_EQ(m.opaque_source_nodes.size(), static_cast<size_t>(N));

    // Every child has a unique index in [0, N)
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(m.node_to_output[children[i]].kind, CompiledOutputKind::SurfacePrimitive)
            << "child " << i << " should have SurfacePrimitive kind";
        uint32_t idx = m.node_to_output[children[i]].index;
        EXPECT_LT(idx, static_cast<uint32_t>(N))
            << "child " << i << " index out of range";
    }

    // Reverse: each opaque_source_nodes[i] is one of the children
    for (int i = 0; i < N; ++i) {
        NodeHandle src = m.opaque_source_nodes[i];
        bool found = false;
        for (int j = 0; j < N; ++j)
            if (children[j] == src) { found = true; break; }
        EXPECT_TRUE(found) << "opaque_source_nodes[" << i << "] not a known child";
    }

    // Forward/reverse round-trip: node_to_output[src] points back to correct slot
    for (int i = 0; i < N; ++i) {
        NodeHandle src = m.opaque_source_nodes[i];
        EXPECT_EQ(m.node_to_output[src].index, static_cast<uint32_t>(i))
            << "round-trip mismatch at slot " << i;
    }
}


// ─── Test 3: Invisible and invalid-mesh nodes produce no compiled output ──────

TEST(SceneMetadata, InvisibleAndInvalidMeshNodesProduceNoOutput)
{
    SceneBuilder b;

    TransformNode root_node{};
    root_node.local = Mat4::identity();
    NodeHandle root_h = add_node(b, root_node);

    TransformNode invisible_node{};
    invisible_node.local = Mat4::identity();
    invisible_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle invisible_h = add_node(b, invisible_node);

    TransformNode bad_mesh_node{};
    bad_mesh_node.local = Mat4::identity();
    bad_mesh_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle bad_mesh_h = add_node(b, bad_mesh_node);

    EXPECT_TRUE(add_edge(b, root_h, invisible_h));
    EXPECT_TRUE(add_edge(b, root_h, bad_mesh_h));

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    SceneStorage scene = std::move(*result);

    std::vector<RenderableDescriptor> descs(node_count(scene.polytree));
    descs[root_h] = RenderableDescriptor{ .node_class = classify_legacy_renderable(RenderPipeline::None) };
    descs[invisible_h] = RenderableDescriptor{
        .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
        .mesh     = 0u,
        .material = 0u,
        .visible  = false,   // invisible — must produce no output
    };
    descs[bad_mesh_h] = RenderableDescriptor{
        .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
        .mesh     = INVALID_MESH,  // invalid mesh — must produce no output
        .material = 0u,
        .visible  = true,
    };
    propagate_all(scene.polytree);

    CompiledSceneStorage storage{};
    compile(storage, scene.polytree, descs, {}, identity_view());

    const CompiledSceneMetadataView& m = storage.metadata;

    // No opaque output should have been emitted
    EXPECT_EQ(storage.scene.opaque.size(), 0u);
    EXPECT_EQ(m.opaque_source_nodes.size(), 0u);

    // Both nodes map to None/invalid
    EXPECT_EQ(m.node_to_output[invisible_h].kind,  CompiledOutputKind::None);
    EXPECT_EQ(m.node_to_output[invisible_h].index, INVALID_COMPILED_OUTPUT);

    EXPECT_EQ(m.node_to_output[bad_mesh_h].kind,  CompiledOutputKind::None);
    EXPECT_EQ(m.node_to_output[bad_mesh_h].index, INVALID_COMPILED_OUTPUT);
}


// ─── Test 5: Metadata buffer reuse ───────────────────────────────────────────
//
// Compiling the same scene twice into the same CompiledSceneStorage must not
// reallocate the metadata_buffer when the required size is unchanged.

TEST(SceneMetadata, MetadataBufferReusedOnSecondCompile)
{
    auto scene = make_root_plus_opaque();
    CompiledSceneStorage storage{};

    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    const std::byte* meta_ptr_first  = storage.metadata_buffer.get();
    const size_t     meta_bytes_first = storage.metadata_bytes;

    ASSERT_NE(meta_ptr_first, nullptr);
    ASSERT_GT(meta_bytes_first, 0u);

    // Second compile — same scene, same sizes.
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    EXPECT_EQ(storage.metadata_buffer.get(), meta_ptr_first)
        << "metadata_buffer reallocated unexpectedly";
    EXPECT_EQ(storage.metadata_bytes, meta_bytes_first)
        << "metadata_bytes changed unexpectedly";
    EXPECT_EQ(storage.metadata_stats.reallocations_this_build, 0u)
        << "metadata_stats reported reallocation on second compile";

    // Metadata must still be correct after the second compile.
    const CompiledSceneMetadataView& m = storage.metadata;
    EXPECT_EQ(m.node_to_output[scene.child].kind,  CompiledOutputKind::SurfacePrimitive);
    EXPECT_EQ(m.node_to_output[scene.child].index, 0u);
    EXPECT_EQ(m.opaque_source_nodes[0], scene.child);
}
