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
#include <limits>
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
    // normalised output is (0,0) = the parameter's neutral, and the bob is still.
    EXPECT_NEAR(params.values[0][0], 0.0f, 0.02f);
    EXPECT_NEAR(params.values[0][1], 0.0f, 0.05f);
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

    // Jerk the anchor sideways: the lagging bob swings, so the output x moves off
    // its ~0 neutral (output is normalised, so the change is O(0.1), not pixels).
    anchors[1] = { 40.0f, 0.0f };
    ino::step_puppet_physics(p, phys, anchors, kDt, params);
    EXPECT_GT(std::abs(params.values[0][0] - before_x), 0.1f);
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
    EXPECT_NEAR(params.values[0][0], 0.0f, 0.05f);
    EXPECT_NEAR(params.values[0][1], 0.0f, 0.1f);
    EXPECT_LT(speed(phys.pendulums[0]), 2.0f);
}

// --- issue #316 C3-C14: the integrator must stay stable at any authored
// --- frequency, and a fault must not latch.
//
// The pendulum is integrated with semi-implicit Euler at a fixed step, which is
// stable only while omega*dt < 2. Nothing in the .inp format, the loader, or the
// editor bounds `frequency`, so an authored value past that limit used to send
// the bob's velocity to inf and then LATCH NaN for the lifetime of the puppet --
// measured at 20 Hz going non-finite on step 117 and never recovering, while
// 19 Hz was stable. 2/(2*pi*dt) is 19.1 Hz at 60 Hz, which is where the two
// measurements bracket.
//
// The sweep runs well past the limit on purpose. "Simplify this to one
// frequency" is exactly the future edit that would silently un-test it.
//
// THE MAGNITUDE BOUNDS BELOW ARE THE LOAD-BEARING ASSERTIONS, not the isfinite
// ones. There are two independent guards here -- the stability cap on omega, and
// the non-finite re-seed at the end of the step -- and the re-seed alone is
// enough to keep every value finite. Checked by neutering the cap: with only
// isfinite assertions this test PASSED against the unfixed integrator, because
// the bob diverged and was re-seeded within the same step and the test never saw
// it. A cap-less pendulum reaches |v| ~ 1e21 before that re-seed fires, so
// bounding the magnitude is what actually distinguishes "stable" from
// "exploding and being papered over every few frames".
TEST(PuppetPhysics, ExtremeFrequencyStaysFiniteAndSelfHeals)
{
    for (const float freq : { 1.0f, 15.0f, 19.0f, 20.0f, 25.0f, 60.0f, 500.0f }) {
        ino::Puppet p = make_pendulum_puppet();
        p.nodes[1].frequency = freq;
        p.nodes[1].physics_model = "SpringPendulum";

        ino::PuppetPhysics phys = ino::make_puppet_physics(p);
        ino::PuppetParams params = ino::make_default_params(p);
        std::vector<std::array<float, 2>> anchors(p.nodes.size(), { 0.0f, 0.0f });

        for (int i = 0; i < 1200; ++i) {
            // Keep the anchor moving so the spring is actually excited.
            anchors[1] = { 20.0f * std::sin(0.05f * static_cast<float>(i)), 0.0f };
            ino::step_puppet_physics(p, phys, anchors, kDt, params);

            ASSERT_TRUE(std::isfinite(phys.pendulums[0].bob[0]))
                << "freq=" << freq << " step=" << i;
            ASSERT_TRUE(std::isfinite(phys.pendulums[0].velocity[0]))
                << "freq=" << freq << " step=" << i;
            ASSERT_TRUE(std::isfinite(params.values[0][0]))
                << "freq=" << freq << " step=" << i;

            // The real pin: bounded, not merely finite. Anchor sway is 20 px on
            // a 50 px rod, so a stable pendulum stays well inside these.
            ASSERT_LT(speed(phys.pendulums[0]), 1.0e5f)
                << "freq=" << freq << " step=" << i << " -- integrator diverged";
            ASSERT_LT(std::hypot(phys.pendulums[0].bob[0] - anchors[1][0],
                                 phys.pendulums[0].bob[1] - anchors[1][1]),
                      1.0e4f)
                << "freq=" << freq << " step=" << i << " -- bob flew off";
        }
    }
}

// --- issue #316 C3-H13: dt is gated in the reject direction ------------------
TEST(PuppetPhysics, NonFiniteDtIsRefused)
{
    ino::Puppet p = make_pendulum_puppet();
    ino::PuppetPhysics phys = ino::make_puppet_physics(p);
    ino::PuppetParams params = ino::make_default_params(p);
    std::vector<std::array<float, 2>> anchors(p.nodes.size(), { 0.0f, 0.0f });

    // Settle first so there is real state to corrupt.
    for (int i = 0; i < 60; ++i) {
        ino::step_puppet_physics(p, phys, anchors, kDt, params);
    }
    const std::array<float, 2> before = phys.pendulums[0].bob;

    ino::step_puppet_physics(
        p, phys, anchors, std::numeric_limits<float>::quiet_NaN(), params);

    // `dt <= 0` was false for NaN, so the step used to run and NaN the bob.
    EXPECT_FLOAT_EQ(phys.pendulums[0].bob[0], before[0]);
    EXPECT_FLOAT_EQ(phys.pendulums[0].bob[1], before[1]);
}

// --- issue #316 C3-H14: a NaN authored field must not reach the integrator ---
TEST(PuppetPhysics, NonFiniteAuthoredFieldsDoNotPoisonThePendulum)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (int which = 0; which < 4; ++which) {
        ino::Puppet p = make_pendulum_puppet();
        switch (which) {
        case 0: p.nodes[1].frequency = nan; break;
        case 1: p.nodes[1].gravity = nan; break;
        case 2: p.nodes[1].angle_damping = nan; break;
        case 3: p.nodes[1].length = nan; break;
        }

        ino::PuppetPhysics phys = ino::make_puppet_physics(p);
        ino::PuppetParams params = ino::make_default_params(p);
        std::vector<std::array<float, 2>> anchors(p.nodes.size(), { 0.0f, 0.0f });

        for (int i = 0; i < 120; ++i) {
            ino::step_puppet_physics(p, phys, anchors, kDt, params);
        }
        EXPECT_TRUE(std::isfinite(phys.pendulums[0].bob[0])) << "field " << which;
        EXPECT_TRUE(std::isfinite(params.values[0][0])) << "field " << which;
    }
}
