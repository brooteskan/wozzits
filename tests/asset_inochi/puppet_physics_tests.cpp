// tests/asset_inochi/puppet_physics_tests.cpp
//
// Pure (device-free) coverage of the SimplePhysics pendulum integrator: node
// discovery, no spurious motion on a static anchor, output response to a moving
// anchor (primary motion), and settling under damping. The exact numbers are
// model-tuned (oracle fidelity is S8); these assert qualitative behaviour.

#include <gtest/gtest.h>

#include <engine/assets/inochi/puppet_physics.h>
#include <engine/assets/inochi/puppet_deform.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    namespace ino = wz::engine::assets::inochi;

    constexpr float kDt = 1.0f / 60.0f;

    // A puppet with one rigid SimplePhysics pendulum (node 1) writing parameter
    // uuid 200 in XY map mode, rest length 50 px.
    ino::Puppet make_pendulum_puppet()
    {
        ino::Puppet p;

        ino::Node root;
        root.kind = ino::NodeKind::Node;
        root.children = { 1 };
        p.nodes.push_back(root);

        ino::Node sp;
        sp.uuid = 100;
        sp.kind = ino::NodeKind::SimplePhysics;
        sp.physics_param = 200;
        sp.physics_model = "Pendulum";
        sp.physics_map_mode = "XY";
        sp.length = 50.0f;
        sp.gravity = 1.0f;
        sp.frequency = 1.0f;
        sp.angle_damping = 0.3f;
        sp.output_scale = { 1.0f, 1.0f };
        p.nodes.push_back(sp);

        ino::Parameter par;
        par.uuid = 200;
        par.name = "phys";
        par.is_vec2 = true;
        par.min = { -1000.0f, -1000.0f };
        par.max = { 1000.0f, 1000.0f };
        par.axis_points[0] = { 0.0f };
        par.axis_points[1] = { 0.0f };
        p.parameters.push_back(par);

        return p;
    }

    float speed(const ino::PendulumState& s)
    {
        return std::hypot(s.velocity[0], s.velocity[1]);
    }
}

TEST(PuppetPhysics, DiscoversSimplePhysicsNodes)
{
    const ino::Puppet p = make_pendulum_puppet();
    const ino::PuppetPhysics phys = ino::make_puppet_physics(p);
    ASSERT_EQ(phys.physics_nodes.size(), 1u);
    EXPECT_EQ(phys.physics_nodes[0], 1u);
    EXPECT_EQ(phys.pendulums.size(), 1u);
}

TEST(PuppetPhysics, StaticAnchorStaysAtRest)
{
    ino::Puppet p = make_pendulum_puppet();
    ino::PuppetPhysics phys = ino::make_puppet_physics(p);
    ino::PuppetParams params = ino::make_default_params(p);

    std::vector<std::array<float, 2>> anchors(p.nodes.size(), { 0.0f, 0.0f });
    for (int i = 0; i < 300; ++i) {
        ino::step_puppet_physics(p, phys, anchors, kDt, params);
    }

    // A rigid pendulum hanging straight below a still anchor gets no swing: the
    // output is the rest offset (0, +length) and the bob is nearly still.
    EXPECT_NEAR(params.values[0][0], 0.0f, 0.5f);
    EXPECT_NEAR(params.values[0][1], 50.0f, 1.0f);
    EXPECT_LT(speed(phys.pendulums[0]), 1.0f);
}

TEST(PuppetPhysics, AnchorMotionDrivesOutput)
{
    ino::Puppet p = make_pendulum_puppet();
    ino::PuppetPhysics phys = ino::make_puppet_physics(p);
    ino::PuppetParams params = ino::make_default_params(p);

    std::vector<std::array<float, 2>> anchors(p.nodes.size(), { 0.0f, 0.0f });
    for (int i = 0; i < 200; ++i) {
        ino::step_puppet_physics(p, phys, anchors, kDt, params);
    }
    const float before_x = params.values[0][0];

    // Jerk the anchor sideways: the lagging bob swings, so the output x moves.
    anchors[1] = { 40.0f, 0.0f };
    ino::step_puppet_physics(p, phys, anchors, kDt, params);
    EXPECT_GT(std::abs(params.values[0][0] - before_x), 5.0f);
}

TEST(PuppetPhysics, SettlesUnderDamping)
{
    ino::Puppet p = make_pendulum_puppet();
    ino::PuppetPhysics phys = ino::make_puppet_physics(p);
    ino::PuppetParams params = ino::make_default_params(p);

    std::vector<std::array<float, 2>> anchors(p.nodes.size(), { 0.0f, 0.0f });
    for (int i = 0; i < 100; ++i) {
        ino::step_puppet_physics(p, phys, anchors, kDt, params);
    }
    // Displace the anchor and hold it: the pendulum swings, then damping settles
    // it to hang straight below the new anchor (output back to the rest offset).
    anchors[1] = { 40.0f, 0.0f };
    for (int i = 0; i < 600; ++i) {
        ino::step_puppet_physics(p, phys, anchors, kDt, params);
    }
    EXPECT_NEAR(params.values[0][0], 0.0f, 1.0f);
    EXPECT_NEAR(params.values[0][1], 50.0f, 2.0f);
    EXPECT_LT(speed(phys.pendulums[0]), 2.0f);
}
