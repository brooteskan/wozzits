// tests/asset_inochi/puppet_deform_tests.cpp
//
// Pure (device-free) coverage of the puppet deformer: default-parameter build +
// clamped set, axis-grid location, nearest vs bilinear sampling, scalar-target
// transform deltas, per-vertex Deform offsets, 2D blend spaces, and accumulation
// of multiple parameters onto one target. Hand-built puppets only; no GPU.

#include <gtest/gtest.h>

#include <engine/assets/inochi/puppet_deform.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
    namespace ino = wz::engine::assets::inochi;

    // A Part node with `vcount` degenerate verts, so vertex_count() == vcount for
    // Deform bindings that target it.
    ino::Node make_part(std::uint32_t uuid, std::size_t vcount)
    {
        ino::Node n;
        n.uuid = uuid;
        n.kind = ino::NodeKind::Part;
        n.mesh.verts.assign(vcount * 2, 0.0f);
        return n;
    }

    // A 1D parameter over x-keys, with a single binding. axis_points[1] carries one
    // key so ny() == 1 (the 1D convention).
    ino::Parameter make_1d_param(
        std::uint32_t uuid, const char* name,
        std::vector<float> x_keys, ino::Binding binding)
    {
        ino::Parameter p;
        p.uuid = uuid;
        p.name = name;
        p.is_vec2 = false;
        p.min = { x_keys.front(), 0.0f };
        p.max = { x_keys.back(), 0.0f };
        p.defaults = { x_keys.front(), 0.0f };
        p.axis_points[0] = std::move(x_keys);
        p.axis_points[1] = { 0.0f };
        p.bindings.push_back(std::move(binding));
        return p;
    }

    ino::Binding scalar_binding(
        std::uint32_t node_uuid, ino::BindTarget target,
        std::vector<float> values,
        ino::InterpolateMode mode = ino::InterpolateMode::Linear)
    {
        ino::Binding b;
        b.node_uuid = node_uuid;
        b.target = target;
        b.interpolate = mode;
        b.scalar_values = std::move(values);
        return b;
    }
}

TEST(PuppetDeform, DefaultParamsMatchClampedDefaults)
{
    ino::Puppet puppet;
    ino::Parameter p;
    p.name = "eye";
    p.min = { 0.0f, 0.0f };
    p.max = { 1.0f, 1.0f };
    p.defaults = { 2.0f, -1.0f };  // out of range on purpose
    p.axis_points[0] = { 0.0f, 1.0f };
    p.axis_points[1] = { 0.0f, 1.0f };
    puppet.parameters.push_back(p);

    const ino::PuppetParams params = ino::make_default_params(puppet);
    ASSERT_EQ(params.values.size(), 1u);
    EXPECT_FLOAT_EQ(params.values[0][0], 1.0f);   // clamped to max
    EXPECT_FLOAT_EQ(params.values[0][1], 0.0f);   // clamped to min
}

TEST(PuppetDeform, SetParamClampsAndReportsUnknown)
{
    ino::Puppet puppet;
    ino::Parameter p;
    p.name = "mouth";
    p.min = { 0.0f, 0.0f };
    p.max = { 1.0f, 0.0f };
    p.axis_points[0] = { 0.0f, 1.0f };
    p.axis_points[1] = { 0.0f };
    puppet.parameters.push_back(p);

    ino::PuppetParams params = ino::make_default_params(puppet);
    EXPECT_TRUE(ino::set_param(puppet, params, "mouth", 5.0f));
    EXPECT_FLOAT_EQ(params.values[0][0], 1.0f);   // clamped to max
    EXPECT_FALSE(ino::set_param(puppet, params, "nope", 0.5f));
}

TEST(PuppetDeform, ScalarBindingLinearInterpolation)
{
    ino::Puppet puppet;
    puppet.nodes.push_back(make_part(10, /*vcount*/ 3));
    puppet.parameters.push_back(make_1d_param(
        1, "tx", { 0.0f, 1.0f },
        scalar_binding(10, ino::BindTarget::TransTX, { 0.0f, 10.0f })));

    ino::PuppetParams params = ino::make_default_params(puppet);
    ino::set_param(puppet, params, "tx", 0.5f);

    const ino::PuppetDeform d = ino::evaluate_puppet_deform(puppet, params);
    ASSERT_EQ(d.transform_deltas.size(), 1u);
    EXPECT_NEAR(d.transform_deltas[0].trans[0], 5.0f, 1e-5f);
    // Endpoints.
    ino::set_param(puppet, params, "tx", 0.0f);
    EXPECT_NEAR(ino::evaluate_puppet_deform(puppet, params)
                    .transform_deltas[0].trans[0], 0.0f, 1e-5f);
    ino::set_param(puppet, params, "tx", 1.0f);
    EXPECT_NEAR(ino::evaluate_puppet_deform(puppet, params)
                    .transform_deltas[0].trans[0], 10.0f, 1e-5f);
}

TEST(PuppetDeform, NearestModeSnapsToClosestCell)
{
    ino::Puppet puppet;
    puppet.nodes.push_back(make_part(10, 1));
    puppet.parameters.push_back(make_1d_param(
        1, "rz", { 0.0f, 1.0f },
        scalar_binding(10, ino::BindTarget::RotZ, { 0.0f, 10.0f },
                       ino::InterpolateMode::Nearest)));

    ino::PuppetParams params = ino::make_default_params(puppet);
    ino::set_param(puppet, params, "rz", 0.4f);   // nearer 0
    EXPECT_NEAR(ino::evaluate_puppet_deform(puppet, params)
                    .transform_deltas[0].rot[2], 0.0f, 1e-5f);
    ino::set_param(puppet, params, "rz", 0.6f);   // nearer 1
    EXPECT_NEAR(ino::evaluate_puppet_deform(puppet, params)
                    .transform_deltas[0].rot[2], 10.0f, 1e-5f);
}

TEST(PuppetDeform, DeformBindingInterpolatesPerVertexOffsets)
{
    ino::Puppet puppet;
    puppet.nodes.push_back(make_part(10, /*vcount*/ 2));

    ino::Binding b;
    b.node_uuid = 10;
    b.target = ino::BindTarget::Deform;
    b.interpolate = ino::InterpolateMode::Linear;
    // cell 0 = no offset; cell 1 = vert0 +(2,0), vert1 +(0,4).
    b.deform_values = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 2.0f, 0.0f, 0.0f, 4.0f },
    };
    puppet.parameters.push_back(
        make_1d_param(1, "blink", { 0.0f, 1.0f }, std::move(b)));

    ino::PuppetParams params = ino::make_default_params(puppet);
    ino::set_param(puppet, params, "blink", 0.5f);

    const ino::PuppetDeform d = ino::evaluate_puppet_deform(puppet, params);
    ASSERT_EQ(d.vertex_offsets.size(), 1u);
    ASSERT_EQ(d.vertex_offsets[0].size(), 2u);
    EXPECT_NEAR(d.vertex_offsets[0][0][0], 1.0f, 1e-5f);
    EXPECT_NEAR(d.vertex_offsets[0][0][1], 0.0f, 1e-5f);
    EXPECT_NEAR(d.vertex_offsets[0][1][0], 0.0f, 1e-5f);
    EXPECT_NEAR(d.vertex_offsets[0][1][1], 2.0f, 1e-5f);
}

TEST(PuppetDeform, TwoDimensionalBilinearBlend)
{
    ino::Puppet puppet;
    puppet.nodes.push_back(make_part(10, 1));

    ino::Parameter p;
    p.uuid = 1;
    p.name = "xy";
    p.is_vec2 = true;
    p.min = { 0.0f, 0.0f };
    p.max = { 1.0f, 1.0f };
    p.axis_points[0] = { 0.0f, 1.0f };
    p.axis_points[1] = { 0.0f, 1.0f };
    // cell = x*ny + y: (0,0)=0, (0,1)=10, (1,0)=20, (1,1)=30.
    p.bindings.push_back(
        scalar_binding(10, ino::BindTarget::TransTX, { 0.0f, 10.0f, 20.0f, 30.0f }));
    puppet.parameters.push_back(p);

    ino::PuppetParams params = ino::make_default_params(puppet);
    ino::set_param(puppet, params, "xy", 0.5f, 0.5f);

    const ino::PuppetDeform d = ino::evaluate_puppet_deform(puppet, params);
    EXPECT_NEAR(d.transform_deltas[0].trans[0], 15.0f, 1e-5f);  // mean of the 4
}

TEST(PuppetDeform, MultipleParametersAccumulateOnOneTarget)
{
    ino::Puppet puppet;
    puppet.nodes.push_back(make_part(10, 1));
    puppet.parameters.push_back(make_1d_param(
        1, "a", { 0.0f, 1.0f },
        scalar_binding(10, ino::BindTarget::TransTX, { 0.0f, 10.0f })));
    puppet.parameters.push_back(make_1d_param(
        2, "b", { 0.0f, 1.0f },
        scalar_binding(10, ino::BindTarget::TransTX, { 0.0f, 6.0f })));

    ino::PuppetParams params = ino::make_default_params(puppet);
    ino::set_param(puppet, params, "a", 0.5f);   // +5
    ino::set_param(puppet, params, "b", 0.5f);   // +3

    const ino::PuppetDeform d = ino::evaluate_puppet_deform(puppet, params);
    EXPECT_NEAR(d.transform_deltas[0].trans[0], 8.0f, 1e-5f);
}

TEST(PuppetDeform, ValueOutsideAxisClampsToEdge)
{
    ino::Puppet puppet;
    puppet.nodes.push_back(make_part(10, 1));
    puppet.parameters.push_back(make_1d_param(
        1, "tx", { 0.0f, 1.0f },
        scalar_binding(10, ino::BindTarget::TransTX, { 3.0f, 7.0f })));

    // Bypass set_param's clamp to prove the sampler itself clamps to the last key.
    ino::PuppetParams params;
    params.values = { { 9.0f, 0.0f } };
    const ino::PuppetDeform d = ino::evaluate_puppet_deform(puppet, params);
    EXPECT_NEAR(d.transform_deltas[0].trans[0], 7.0f, 1e-5f);
}

// --- issue #316 C3-C7: axis keys are NORMALIZED, values are in [min,max] ------
//
// Every test above uses min=0,max=1 with axis keys {0,1}, which makes the two
// spaces coincide -- which is exactly why the whole suite passed while 28 of
// Aka.inp's 34 parameters sampled the wrong cell. The parameter below is the
// shape a real puppet uses: a symmetric bidirectional range whose axis keys run
// 0..1 and whose neutral value sits at the CENTRE of the grid.
TEST(PuppetDeform, BidirectionalParameterCentresOnItsNeutralValue)
{
    ino::Puppet puppet;
    puppet.nodes.push_back(make_part(10, 1));

    // Head::Yaw-Pitch in miniature: value in [-1,1], keys normalized 0..1.
    ino::Parameter p;
    p.uuid = 1;
    p.name = "yaw";
    p.is_vec2 = false;
    p.min = { -1.0f, 0.0f };
    p.max = { 1.0f, 0.0f };
    p.defaults = { 0.0f, 0.0f };
    p.axis_points[0] = { 0.0f, 0.5f, 1.0f };
    p.axis_points[1] = { 0.0f };
    p.bindings.push_back(
        scalar_binding(10, ino::BindTarget::TransTX, { 0.0f, 10.0f, 20.0f }));
    puppet.parameters.push_back(std::move(p));

    ino::PuppetParams params = ino::make_default_params(puppet);

    // The neutral value is the MIDDLE key, not the first one. Before the fix
    // this returned 0: the raw -1..1 value was compared against 0..1 keys, so
    // everything at or below 0 collapsed onto keys.front().
    EXPECT_NEAR(ino::evaluate_puppet_deform(puppet, params)
                    .transform_deltas[0].trans[0], 10.0f, 1e-5f);

    // The negative half must be live, not folded onto the first cell.
    ino::set_param(puppet, params, "yaw", -1.0f);
    EXPECT_NEAR(ino::evaluate_puppet_deform(puppet, params)
                    .transform_deltas[0].trans[0], 0.0f, 1e-5f);
    ino::set_param(puppet, params, "yaw", -0.5f);
    EXPECT_NEAR(ino::evaluate_puppet_deform(puppet, params)
                    .transform_deltas[0].trans[0], 5.0f, 1e-5f);
    ino::set_param(puppet, params, "yaw", 0.5f);
    EXPECT_NEAR(ino::evaluate_puppet_deform(puppet, params)
                    .transform_deltas[0].trans[0], 15.0f, 1e-5f);
    ino::set_param(puppet, params, "yaw", 1.0f);
    EXPECT_NEAR(ino::evaluate_puppet_deform(puppet, params)
                    .transform_deltas[0].trans[0], 20.0f, 1e-5f);
}

// --- issue #316 C3-C15: a non-finite parameter must not be storable ----------
TEST(PuppetDeform, SetParamRefusesNonFinite)
{
    ino::Puppet puppet;
    puppet.nodes.push_back(make_part(10, 1));
    puppet.parameters.push_back(make_1d_param(
        1, "tx", { 0.0f, 1.0f },
        scalar_binding(10, ino::BindTarget::TransTX, { 0.0f, 10.0f })));

    ino::PuppetParams params = ino::make_default_params(puppet);
    ASSERT_TRUE(ino::set_param(puppet, params, "tx", 0.5f));
    const float kept = params.values[0][0];

    // Refused, and the previous value survives. clampf's guard would otherwise
    // land the parameter on `min`, which for a bidirectional range is a visible
    // pose change rather than "nothing happened".
    EXPECT_FALSE(ino::set_param(
        puppet, params, "tx", std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FLOAT_EQ(params.values[0][0], kept);
    EXPECT_FALSE(ino::set_param(
        puppet, params, "tx", std::numeric_limits<float>::infinity()));
    EXPECT_FLOAT_EQ(params.values[0][0], kept);

    // And the deform stays finite.
    const ino::PuppetDeform d = ino::evaluate_puppet_deform(puppet, params);
    EXPECT_TRUE(std::isfinite(d.transform_deltas[0].trans[0]));
}
