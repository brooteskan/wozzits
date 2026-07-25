// tests/asset_inochi/puppet_draw_list_tests.cpp
//
// Pure (device-free) coverage of build_puppet_draw_list: the static flatten of a
// Puppet into draw-ordered Parts -- transform accumulation, zsort ordering,
// pos/uv interleave, atlas/opacity/blend carry-through, disabled/empty skipping,
// and puppet-space bounds. No GPU, no fixture: hand-built puppets only.

#include <gtest/gtest.h>

#include <engine/assets/inochi/puppet_draw_list.h>
#include <engine/assets/inochi/puppet_deform.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{
    namespace ino = wz::engine::assets::inochi;

    constexpr float kHalfPi = 1.5707963267948966f;

    // A unit right-triangle Part in local pixel space: verts (0,0),(10,0),(0,10)
    // with matching corner UVs, one atlas page.
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
}

TEST(PuppetDrawList, FlattensAndOrdersByZsort)
{
    ino::Puppet puppet;

    ino::Node root;
    root.kind = ino::NodeKind::Node;
    root.transform.trans = { 100.0f, 50.0f, 0.0f };
    root.children = { 1, 2 };
    puppet.nodes.push_back(root);

    ino::Node front = make_triangle_part(11, /*zsort*/ 1.0f, /*atlas*/ 2);
    front.transform.trans = { 10.0f, 0.0f, 0.0f };
    front.opacity = 0.5f;
    front.blend_mode = ino::BlendMode::Multiply;
    puppet.nodes.push_back(front);

    ino::Node back = make_triangle_part(12, /*zsort*/ -1.0f, /*atlas*/ 0);
    puppet.nodes.push_back(back);

    const ino::PuppetDrawList list = ino::build_puppet_draw_list(puppet);

    ASSERT_EQ(list.parts.size(), 2u);
    // Ascending zsort => the -1 Part (node index 2) draws first (back).
    EXPECT_EQ(list.parts[0].node_index, 2u);
    EXPECT_EQ(list.parts[1].node_index, 1u);
    EXPECT_FLOAT_EQ(list.parts[0].zsort, -1.0f);
    EXPECT_FLOAT_EQ(list.parts[1].zsort, 1.0f);

    // Carry-through on the front Part.
    const ino::PuppetPartDraw& fp = list.parts[1];
    EXPECT_EQ(fp.atlas_texture, 2u);
    EXPECT_FLOAT_EQ(fp.opacity, 0.5f);
    EXPECT_EQ(fp.blend, ino::BlendMode::Multiply);

    // Interleaved pos+uv, indices preserved.
    ASSERT_EQ(fp.vertices.size(), 3u);
    EXPECT_FLOAT_EQ(fp.vertices[1].px, 10.0f);
    EXPECT_FLOAT_EQ(fp.vertices[1].py, 0.0f);
    EXPECT_FLOAT_EQ(fp.vertices[1].u, 1.0f);
    EXPECT_FLOAT_EQ(fp.vertices[1].v, 0.0f);
    EXPECT_EQ(fp.indices, (std::vector<std::uint32_t>{ 0u, 1u, 2u }));
}

TEST(PuppetDrawList, AccumulatesParentTransform)
{
    ino::Puppet puppet;

    ino::Node root;
    root.kind = ino::NodeKind::Node;
    root.transform.trans = { 100.0f, 50.0f, 0.0f };
    root.children = { 1 };
    puppet.nodes.push_back(root);

    ino::Node part = make_triangle_part(11, 0.0f, 0);
    part.transform.trans = { 10.0f, 5.0f, 0.0f };
    puppet.nodes.push_back(part);

    const ino::PuppetDrawList list = ino::build_puppet_draw_list(puppet);
    ASSERT_EQ(list.parts.size(), 1u);

    // Part-local origin (0,0) maps to root.trans + part.trans.
    const std::array<float, 2> o = ino::apply(list.parts[0].placement, 0.0f, 0.0f);
    EXPECT_NEAR(o[0], 110.0f, 1e-4f);
    EXPECT_NEAR(o[1], 55.0f, 1e-4f);
    // Local (10,0) shifts by the same offset (no rotation/scale).
    const std::array<float, 2> p = ino::apply(list.parts[0].placement, 10.0f, 0.0f);
    EXPECT_NEAR(p[0], 120.0f, 1e-4f);
    EXPECT_NEAR(p[1], 55.0f, 1e-4f);
}

TEST(PuppetDrawList, ComposesRotation)
{
    ino::Puppet puppet;

    ino::Node root;
    root.kind = ino::NodeKind::Node;
    root.transform.rot = { 0.0f, 0.0f, kHalfPi };  // +90 deg about Z
    root.children = { 1 };
    puppet.nodes.push_back(root);

    puppet.nodes.push_back(make_triangle_part(11, 0.0f, 0));

    const ino::PuppetDrawList list = ino::build_puppet_draw_list(puppet);
    ASSERT_EQ(list.parts.size(), 1u);

    // (1,0) rotated +90 deg CCW -> (0,1).
    const std::array<float, 2> p = ino::apply(list.parts[0].placement, 1.0f, 0.0f);
    EXPECT_NEAR(p[0], 0.0f, 1e-4f);
    EXPECT_NEAR(p[1], 1.0f, 1e-4f);
}

TEST(PuppetDrawList, SkipsDisabledAndEmptyParts)
{
    ino::Puppet puppet;

    ino::Node root;
    root.kind = ino::NodeKind::Node;
    root.children = { 1, 2, 3 };
    puppet.nodes.push_back(root);

    ino::Node disabled = make_triangle_part(11, 0.0f, 0);
    disabled.enabled = false;
    puppet.nodes.push_back(disabled);

    ino::Node empty;
    empty.kind = ino::NodeKind::Part;  // no mesh
    puppet.nodes.push_back(empty);

    puppet.nodes.push_back(make_triangle_part(13, 0.0f, 0));

    const ino::PuppetDrawList list = ino::build_puppet_draw_list(puppet);
    ASSERT_EQ(list.parts.size(), 1u);
    EXPECT_EQ(list.parts[0].node_index, 3u);
}

TEST(PuppetDrawList, AppliesDeformToTransformAndVertices)
{
    ino::Puppet puppet;

    ino::Node root;
    root.kind = ino::NodeKind::Node;
    root.children = { 1 };
    puppet.nodes.push_back(root);

    puppet.nodes.push_back(make_triangle_part(11, 0.0f, 0));  // verts (0,0),(10,0),(0,10)

    // Deform: shift the Part +100 in x (transform delta) and push vertex 0 by (2,0).
    ino::PuppetDeform deform;
    deform.transform_deltas.resize(2);
    deform.vertex_offsets.resize(2);
    deform.transform_deltas[1].trans = { 100.0f, 0.0f, 0.0f };
    deform.vertex_offsets[1] = { { 2.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } };

    const ino::PuppetDrawList list = ino::build_puppet_draw_list(puppet, &deform);
    ASSERT_EQ(list.parts.size(), 1u);

    // Vertex 0 local pos carries the (2,0) offset; placement adds the +100 shift.
    const ino::PuppetPartDraw& p = list.parts[0];
    EXPECT_NEAR(p.vertices[0].px, 2.0f, 1e-4f);
    EXPECT_NEAR(p.vertices[0].py, 0.0f, 1e-4f);
    const std::array<float, 2> w =
        ino::apply(p.placement, p.vertices[0].px, p.vertices[0].py);
    EXPECT_NEAR(w[0], 102.0f, 1e-4f);
    EXPECT_NEAR(w[1], 0.0f, 1e-4f);

    // A null deform reproduces the rest pose (vertex 0 at local origin).
    const ino::PuppetDrawList rest = ino::build_puppet_draw_list(puppet);
    EXPECT_NEAR(rest.parts[0].vertices[0].px, 0.0f, 1e-4f);
}

TEST(PuppetDrawList, ComputesBounds)
{
    ino::Puppet puppet;

    ino::Node root;
    root.kind = ino::NodeKind::Node;
    root.transform.trans = { 100.0f, 200.0f, 0.0f };
    root.children = { 1 };
    puppet.nodes.push_back(root);

    puppet.nodes.push_back(make_triangle_part(11, 0.0f, 0));  // verts (0,0),(10,0),(0,10)

    const ino::PuppetDrawList list = ino::build_puppet_draw_list(puppet);
    EXPECT_NEAR(list.bounds_min[0], 100.0f, 1e-4f);
    EXPECT_NEAR(list.bounds_min[1], 200.0f, 1e-4f);
    EXPECT_NEAR(list.bounds_max[0], 110.0f, 1e-4f);
    EXPECT_NEAR(list.bounds_max[1], 210.0f, 1e-4f);
}
