#pragma once

// engine/assets/inochi/puppet_physics.h
//
// SimplePhysics idle motion for an Inochi2D puppet (seam S7): each SimplePhysics
// node is a damped pendulum whose bob lags a moving anchor; the bob's offset from
// the anchor maps (XY or AngleLength) to the node's output parameter, which then
// drives the deformer. Pure CPU -- the anchor world positions are an INPUT, so the
// integrator is device-free and unit-testable.
//
// A pendulum needs PRIMARY motion (a moving anchor) to sway; a perfectly static
// puppet settles to its gravity rest and stops. The renderer supplies anchor
// motion (e.g. a subtle breathing bob) in the wiring seam. The numeric model is
// physically plausible but not yet oracle-exact against Inochi Creator -- the
// fidelity pass is S8. See the inochi-runtime-track.

#include <engine/assets/inochi/inochi_puppet.h>
#include <engine/assets/inochi/puppet_deform.h>   // PuppetParams

#include <array>
#include <cstddef>
#include <vector>

namespace wz::engine::assets::inochi
{
    // Persistent integrator state for one SimplePhysics node: its pendulum bob in
    // puppet-pixel space and the bob's velocity. `initialized` is false until the
    // first step seeds the bob at the anchor's rest position.
    struct PendulumState
    {
        std::array<float, 2> bob{ 0.0f, 0.0f };
        std::array<float, 2> velocity{ 0.0f, 0.0f };
        bool initialized = false;
    };

    // Physics state for a whole puppet: one PendulumState per SimplePhysics node,
    // with the parallel node indices those pendulums belong to.
    struct PuppetPhysics
    {
        std::vector<PendulumState> pendulums;    // parallel to physics_nodes
        std::vector<std::size_t> physics_nodes;  // indices into Puppet::nodes
    };

    // Build the physics state for a puppet: collect its SimplePhysics nodes (those
    // with a valid output parameter) and seed one uninitialised pendulum each.
    [[nodiscard]] PuppetPhysics make_puppet_physics(const Puppet& puppet);

    // Advance the physics by `dt` seconds and write each SimplePhysics node's output
    // parameter into `params`. `node_anchors` is each node's current world position
    // in puppet-pixel space (index parallel to Puppet::nodes) -- this is where
    // primary motion enters, so a moving anchor makes the bob swing. Each pendulum
    // integrates a spring-to-rest-length + gravity + damping model (rigid for
    // "Pendulum", stretchy for "SpringPendulum"), maps the bob offset via the node's
    // map mode + output_scale, and sets the output parameter (clamped to its range).
    void step_puppet_physics(
        const Puppet& puppet,
        PuppetPhysics& physics,
        const std::vector<std::array<float, 2>>& node_anchors,
        float dt,
        PuppetParams& params);
}
