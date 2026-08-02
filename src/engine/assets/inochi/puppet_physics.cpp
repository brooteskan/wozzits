// src/engine/assets/inochi/puppet_physics.cpp
//
// SimplePhysics pendulum integration for an Inochi2D puppet. Each SimplePhysics
// node is a damped pendulum anchored at the node's world position; the bob lags
// the anchor and its offset maps to the node's output parameter. Pure CPU; the
// anchor positions are supplied by the caller (that is where primary motion, e.g.
// a breathing bob, enters). See puppet_physics.h.
//
// The numeric constants below are tuned for stable, visible motion, not for exact
// parity with Inochi Creator -- the oracle fidelity pass is S8.

#include <engine/assets/inochi/puppet_physics.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace wz::engine::assets::inochi
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;
        // Gravity acceleration in puppet-pixel units at gravity == 1. Puppets are
        // ~1000 px tall, so this gives pendulum periods around a second.
        constexpr float kGravityAccel = 980.0f;
        // Maps the authored [0,1] damping onto a per-second velocity decay.
        constexpr float kDampRate = 6.0f;
        constexpr float kMinDist = 1.0e-3f;

        float length2(const std::array<float, 2>& v) noexcept
        {
            return std::sqrt(v[0] * v[0] + v[1] * v[1]);
        }
    } // namespace

    PuppetPhysics make_puppet_physics(const Puppet& puppet)
    {
        PuppetPhysics physics;
        for (std::size_t i = 0; i < puppet.nodes.size(); ++i) {
            const Node& node = puppet.nodes[i];
            if (node.kind == NodeKind::SimplePhysics && node.physics_param != 0) {
                physics.physics_nodes.push_back(i);
                physics.pendulums.emplace_back();
            }
        }
        return physics;
    }

    void step_puppet_physics(
        const Puppet& puppet,
        PuppetPhysics& physics,
        const std::vector<std::array<float, 2>>& node_anchors,
        float dt,
        PuppetParams& params)
    {
        // Reject direction (`!(dt > 0)`, not `dt <= 0`): every comparison with
        // NaN is false, so the accept spelling let a NaN dt through and NaNed
        // every pendulum on the first call (issue #316, C3-H13).
        if (!(dt > 0.0f)) {
            return;
        }

        for (std::size_t pi = 0; pi < physics.physics_nodes.size(); ++pi) {
            const std::size_t node_index = physics.physics_nodes[pi];
            if (node_index >= puppet.nodes.size()
                || node_index >= node_anchors.size())
            {
                continue;
            }
            const Node& sp = puppet.nodes[node_index];
            PendulumState& s = physics.pendulums[pi];

            const std::array<float, 2> anchor = node_anchors[node_index];
            const float rest = sp.length > 0.0f ? sp.length : 1.0f;

            if (!s.initialized) {
                // Seed the bob hanging straight below the anchor (+y is down in
                // puppet-pixel space), at rest -- a static puppet then stays put.
                s.bob = { anchor[0], anchor[1] + rest };
                s.velocity = { 0.0f, 0.0f };
                s.initialized = true;
            }

            std::array<float, 2> d = { s.bob[0] - anchor[0], s.bob[1] - anchor[1] };
            float dist = std::max(length2(d), kMinDist);
            std::array<float, 2> dir = { d[0] / dist, d[1] / dist };

            // Spring toward the rest length (stiffness from the authored frequency)
            // plus gravity. For a rigid Pendulum we also reproject below; the spring
            // keeps the pre-projection integration well-behaved.
            // std::max(0.0f, x) not std::max(x, 0.0f): the standard returns the
            // FIRST argument when the comparison is false, so with a NaN authored
            // field the value-first spelling returns NaN and the zero-first
            // spelling returns 0. Three of the four guards here were value-first;
            // `rest` above was already written safely (issue #316, C3-H14).
            //
            // The stiffness is additionally capped at the stability limit of the
            // semi-implicit Euler step below. That integrator is stable only
            // while omega*dt < 2; past it the bob's velocity grows without bound,
            // reaches inf, and the state then LATCHES NaN forever because every
            // subsequent update is NaN-in/NaN-out. Measured at the shipped
            // breathing anchor and dt = 1/60: frequency 19 Hz is stable and
            // self-heals, 20 Hz goes non-finite at step 117 and never recovers
            // (issue #316, C3-C14) -- 2/(2*pi*dt) is 19.1 Hz, so the cap lands
            // exactly where the measurement did. Nothing bounds `frequency` in
            // the .inp format, the loader, or the editor.
            const float omega_max = 2.0f / dt;
            const float omega =
                std::min(2.0f * kPi * std::max(0.0f, sp.frequency), omega_max);
            const float k = omega * omega;
            std::array<float, 2> force = {
                -dir[0] * (dist - rest) * k,
                -dir[1] * (dist - rest) * k,
            };
            force[1] += kGravityAccel * std::max(0.0f, sp.gravity);

            s.velocity[0] += force[0] * dt;
            s.velocity[1] += force[1] * dt;

            const float damp = std::clamp(
                1.0f - std::max(0.0f, sp.angle_damping) * dt * kDampRate,
                0.0f, 1.0f);
            s.velocity[0] *= damp;
            s.velocity[1] *= damp;

            s.bob[0] += s.velocity[0] * dt;
            s.bob[1] += s.velocity[1] * dt;

            // A transient fault must stay transient. The guards above stop the
            // known routes to a non-finite state, but this integrator feeds its
            // own output back in, so ANY non-finite value that reaches it -- an
            // anchor from a NaN transform, a hostile authored field this file
            // does not own -- would otherwise persist for the lifetime of the
            // puppet with no way back. Re-seeding costs one pendulum one frame
            // of motion; latching costs the puppet the rest of the session.
            // Same ruling as the motion filter's (commit 7551bd40).
            if (!std::isfinite(s.bob[0]) || !std::isfinite(s.bob[1])
                || !std::isfinite(s.velocity[0]) || !std::isfinite(s.velocity[1]))
            {
                s.bob = { anchor[0], anchor[1] + rest };
                s.velocity = { 0.0f, 0.0f };
            }

            // "SpringPendulum" lets the rod stretch; anything else ("Pendulum")
            // is rigid -- reproject the bob to the rest length and drop the radial
            // velocity so only the swing (tangential) motion survives.
            if (sp.physics_model != "SpringPendulum") {
                std::array<float, 2> nd = {
                    s.bob[0] - anchor[0], s.bob[1] - anchor[1] };
                const float ndist = std::max(length2(nd), kMinDist);
                const std::array<float, 2> ndir = { nd[0] / ndist, nd[1] / ndist };
                s.bob = { anchor[0] + ndir[0] * rest, anchor[1] + ndir[1] * rest };
                const float radial =
                    s.velocity[0] * ndir[0] + s.velocity[1] * ndir[1];
                s.velocity[0] -= radial * ndir[0];
                s.velocity[1] -= radial * ndir[1];
            }

            const std::array<float, 2> off = {
                s.bob[0] - anchor[0], s.bob[1] - anchor[1] };

            // Map the bob offset to the output parameter, NORMALISED so that a
            // settled pendulum (bob hanging straight down at the rest length)
            // outputs (0,0) -- the parameter's neutral. Inochi parameters are
            // normalised (~[-1,1]); emitting the raw pixel offset (e.g. (0, 50) at
            // rest) clamps the binding to its grid extreme -> a fixed slumped pose.
            // A rigid pendulum keeps |off| == rest, so off/rest is a unit vector.
            std::array<float, 2> out{};
            if (sp.physics_map_mode == "AngleLength") {
                const float angle = std::atan2(off[0], off[1]);      // 0 at rest
                const float len_ratio = length2(off) / rest - 1.0f;  // 0 at rest
                out = { angle * sp.output_scale[0], len_ratio * sp.output_scale[1] };
            }
            else {
                // XY (default): normalised offset with the straight-down rest
                // subtracted, so rest -> (0,0).
                out = {
                    (off[0] / rest) * sp.output_scale[0],
                    (off[1] / rest - 1.0f) * sp.output_scale[1],
                };
            }

            set_param_uuid(puppet, params, sp.physics_param, out[0], out[1]);
        }
    }
}
