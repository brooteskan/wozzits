#pragma once

// engine/assets/inochi/puppet_deform.h
//
// The parameter / blend-space deformer for an Inochi2D puppet (seam S3): evaluate
// a set of parameter values against the puppet's bindings to produce per-node
// transform deltas + per-Part vertex offsets, which the draw list applies to move
// the puppet. PURE CPU (no GPU). This is the GENERAL animation spine -- Inochi
// parameters are morph / blend-space weights (== glTF morph-target weights), so a
// later GLB morph path reuses the same evaluate->apply substrate. See the
// inochi-runtime-track.

#include <engine/assets/inochi/inochi_puppet.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace wz::engine::assets::inochi
{
    // Live parameter values, one (x,y) per Puppet::parameters entry (parallel by
    // index; 1D parameters use [0] and ignore [1]). Build with make_default_params,
    // then set_param; the evaluator reads it. Kept as plain data so a driver
    // (behavior, physics, time) can own and mutate it however it likes.
    struct PuppetParams
    {
        std::vector<std::array<float, 2>> values;  // parallel to Puppet::parameters
    };

    // Initialise every parameter to its authored default, clamped into [min,max].
    [[nodiscard]] PuppetParams make_default_params(const Puppet& puppet);

    // Set a parameter's value by name, clamped to its [min,max]. `y` is ignored for
    // a 1D parameter. Returns false (values unchanged) if no parameter has that name.
    bool set_param(const Puppet& puppet, PuppetParams& params,
                   std::string_view name, float x, float y = 0.0f);

    // As set_param, addressed by parameter uuid.
    bool set_param_uuid(const Puppet& puppet, PuppetParams& params,
                        std::uint32_t uuid, float x, float y = 0.0f);

    // An additive delta to a node's base Transform. trans/rot are ADDED to the base
    // components; `scale` is ADDED to the base scale (delta 0 => base scale kept);
    // `zsort` is added to the base zsort. Matches Inochi's parameter offsets, which
    // accumulate on top of the rigged rest pose.
    struct TransformDelta
    {
        std::array<float, 3> trans{ 0.0f, 0.0f, 0.0f };
        std::array<float, 3> rot{ 0.0f, 0.0f, 0.0f };
        std::array<float, 2> scale{ 0.0f, 0.0f };
        float zsort = 0.0f;
    };

    // The evaluated deformation of a puppet for a given parameter set: per-node
    // transform deltas + per-node (Part) vertex offsets, both indexed by node index
    // (parallel to Puppet::nodes). vertex_offsets[i] is empty for a node with no
    // Deform binding driven this frame; otherwise it holds one (dx,dy) per vertex of
    // that Part's mesh (Part-local pixels), to be added before placement.
    struct PuppetDeform
    {
        std::vector<TransformDelta> transform_deltas;                  // size = nodes.size()
        std::vector<std::vector<std::array<float, 2>>> vertex_offsets;  // size = nodes.size()

        [[nodiscard]] bool empty() const noexcept { return transform_deltas.empty(); }
    };

    // Evaluate `params` against every parameter binding in `puppet` and accumulate
    // the result. For each parameter, its (x,y) value is located in that parameter's
    // axis grid (axis_points[0]=x keys, [1]=y keys; cell index = x*ny + y) and each
    // binding is sampled there: Nearest picks the closest cell, Linear does bilinear
    // interpolation, Cubic falls back to Linear for now. Scalar targets (translate/
    // rotate/scale/zsort) accumulate into the target node's TransformDelta; Deform
    // targets accumulate per-vertex (dx,dy) offsets on the target Part. Multiple
    // parameters driving the same node or vertices SUM. Pure; no GPU. The returned
    // deform is sized to puppet.nodes -- apply it via build_puppet_draw_list.
    [[nodiscard]] PuppetDeform evaluate_puppet_deform(
        const Puppet& puppet, const PuppetParams& params);
}
