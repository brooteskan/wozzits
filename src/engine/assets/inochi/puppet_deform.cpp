// src/engine/assets/inochi/puppet_deform.cpp
//
// Evaluate an Inochi2D puppet's parameter bindings into per-node transform deltas
// and per-Part vertex offsets. Pure CPU: for each parameter, locate its value in
// the parameter's axis grid, sample every binding there (nearest / bilinear), and
// accumulate. Scalar targets offset the node transform; Deform targets offset the
// Part's vertices. Multiple parameters driving the same target sum. See
// puppet_deform.h.

#include <engine/assets/inochi/puppet_deform.h>

#include <cstddef>
#include <unordered_map>

namespace wz::engine::assets::inochi
{
    namespace
    {
        // Clamp without assuming lo <= hi (authored min/max could be inverted):
        // returns lo when v < lo, hi when v > hi, else v.
        float clampf(float v, float lo, float hi) noexcept
        {
            if (v < lo) return lo;
            if (v > hi) return hi;
            return v;
        }

        // Locate a value on a sorted axis: the bracketing key indices + the [0,1]
        // fraction between them. Clamps outside the range (t=0 at/below the first
        // key, t=1 at/above the last). A single-key (or empty) axis collapses to
        // {0,0,0}. axis_points are authored ascending.
        struct AxisLoc
        {
            std::size_t i0 = 0;
            std::size_t i1 = 0;
            float t = 0.0f;
        };

        AxisLoc locate_axis(const std::vector<float>& keys, float v) noexcept
        {
            const std::size_t n = keys.size();
            if (n <= 1) {
                return { 0, 0, 0.0f };
            }
            if (v <= keys.front()) {
                return { 0, 1, 0.0f };
            }
            if (v >= keys.back()) {
                return { n - 2, n - 1, 1.0f };
            }
            std::size_t i = 0;
            while (i + 1 < n && keys[i + 1] <= v) {
                ++i;
            }
            const float span = keys[i + 1] - keys[i];
            const float t = span > 0.0f ? (v - keys[i]) / span : 0.0f;
            return { i, i + 1, t };
        }

        // One weighted grid cell of a binding sample.
        struct Corner
        {
            std::size_t cell = 0;
            float w = 0.0f;
        };

        // The (up to) four bilinear corners for a sample, given the located x/y axes
        // and the binding's interpolation mode. Cell index = x*ny + y (matches
        // Binding grid layout). Nearest snaps each fraction to 0/1; Cubic falls back
        // to Linear for now. Zero-weight corners are harmless (they contribute 0).
        std::array<Corner, 4> sample_corners(
            const AxisLoc& x, const AxisLoc& y, std::size_t ny,
            InterpolateMode mode) noexcept
        {
            float ax = x.t;
            float ay = y.t;
            if (mode == InterpolateMode::Nearest) {
                ax = x.t < 0.5f ? 0.0f : 1.0f;
                ay = y.t < 0.5f ? 0.0f : 1.0f;
            }
            const float w00 = (1.0f - ax) * (1.0f - ay);
            const float w10 = ax * (1.0f - ay);
            const float w01 = (1.0f - ax) * ay;
            const float w11 = ax * ay;
            return { {
                { x.i0 * ny + y.i0, w00 },
                { x.i1 * ny + y.i0, w10 },
                { x.i0 * ny + y.i1, w01 },
                { x.i1 * ny + y.i1, w11 },
            } };
        }

        // Accumulate a scalar binding sampled at `corners` into the matching field
        // of the node's transform delta.
        void accumulate_scalar(
            const Binding& b, const std::array<Corner, 4>& corners,
            TransformDelta& delta) noexcept
        {
            float value = 0.0f;
            for (const Corner& c : corners) {
                if (c.w != 0.0f && c.cell < b.scalar_values.size()) {
                    value += c.w * b.scalar_values[c.cell];
                }
            }
            switch (b.target) {
            case BindTarget::TransTX: delta.trans[0] += value; break;
            case BindTarget::TransTY: delta.trans[1] += value; break;
            case BindTarget::TransTZ: delta.trans[2] += value; break;
            case BindTarget::RotX:    delta.rot[0] += value; break;
            case BindTarget::RotY:    delta.rot[1] += value; break;
            case BindTarget::RotZ:    delta.rot[2] += value; break;
            case BindTarget::ScaleX:  delta.scale[0] += value; break;
            case BindTarget::ScaleY:  delta.scale[1] += value; break;
            case BindTarget::ZSort:   delta.zsort += value; break;
            default: break;
            }
        }

        // Accumulate a Deform binding sampled at `corners` into a node's per-vertex
        // offsets. vcount comes from the target Part's mesh; each corner cell holds a
        // flat 2*vcount (dx,dy) array. Out-of-range cells/vertices contribute 0.
        void accumulate_deform(
            const Binding& b, const std::array<Corner, 4>& corners,
            std::size_t vcount,
            std::vector<std::array<float, 2>>& offsets)
        {
            if (vcount == 0) {
                return;
            }
            if (offsets.empty()) {
                offsets.assign(vcount, { 0.0f, 0.0f });
            }
            for (const Corner& c : corners) {
                if (c.w == 0.0f || c.cell >= b.deform_values.size()) {
                    continue;
                }
                const std::vector<float>& cell = b.deform_values[c.cell];
                const std::size_t pairs = cell.size() / 2;
                const std::size_t count = pairs < vcount ? pairs : vcount;
                for (std::size_t vi = 0; vi < count; ++vi) {
                    offsets[vi][0] += c.w * cell[vi * 2 + 0];
                    offsets[vi][1] += c.w * cell[vi * 2 + 1];
                }
            }
        }
    } // namespace

    PuppetParams make_default_params(const Puppet& puppet)
    {
        PuppetParams params;
        params.values.resize(puppet.parameters.size());
        for (std::size_t i = 0; i < puppet.parameters.size(); ++i) {
            const Parameter& p = puppet.parameters[i];
            params.values[i] = {
                clampf(p.defaults[0], p.min[0], p.max[0]),
                clampf(p.defaults[1], p.min[1], p.max[1]),
            };
        }
        return params;
    }

    namespace
    {
        bool set_param_at(
            const Puppet& puppet, PuppetParams& params, std::size_t index,
            float x, float y)
        {
            if (index >= puppet.parameters.size()) {
                return false;
            }
            if (params.values.size() != puppet.parameters.size()) {
                params.values.resize(puppet.parameters.size());
            }
            const Parameter& p = puppet.parameters[index];
            params.values[index] = {
                clampf(x, p.min[0], p.max[0]),
                clampf(y, p.min[1], p.max[1]),
            };
            return true;
        }
    } // namespace

    bool set_param(const Puppet& puppet, PuppetParams& params,
                   std::string_view name, float x, float y)
    {
        for (std::size_t i = 0; i < puppet.parameters.size(); ++i) {
            if (puppet.parameters[i].name == name) {
                return set_param_at(puppet, params, i, x, y);
            }
        }
        return false;
    }

    bool set_param_uuid(const Puppet& puppet, PuppetParams& params,
                        std::uint32_t uuid, float x, float y)
    {
        for (std::size_t i = 0; i < puppet.parameters.size(); ++i) {
            if (puppet.parameters[i].uuid == uuid) {
                return set_param_at(puppet, params, i, x, y);
            }
        }
        return false;
    }

    PuppetDeform evaluate_puppet_deform(
        const Puppet& puppet, const PuppetParams& params)
    {
        PuppetDeform out;
        const std::size_t node_count = puppet.nodes.size();
        out.transform_deltas.resize(node_count);
        out.vertex_offsets.resize(node_count);
        if (node_count == 0) {
            return out;
        }

        std::unordered_map<std::uint32_t, std::size_t> node_by_uuid;
        node_by_uuid.reserve(node_count);
        for (std::size_t i = 0; i < node_count; ++i) {
            node_by_uuid.emplace(puppet.nodes[i].uuid, i);
        }

        for (std::size_t pi = 0; pi < puppet.parameters.size(); ++pi) {
            const Parameter& p = puppet.parameters[pi];
            if (p.axis_points[0].empty()) {
                continue;
            }
            const std::array<float, 2> value =
                pi < params.values.size() ? params.values[pi] : p.defaults;

            const std::size_t ny = p.ny() == 0 ? 1 : p.ny();
            const AxisLoc xloc = locate_axis(p.axis_points[0], value[0]);
            const AxisLoc yloc =
                p.ny() >= 2 ? locate_axis(p.axis_points[1], value[1]) : AxisLoc{};

            for (const Binding& b : p.bindings) {
                const auto it = node_by_uuid.find(b.node_uuid);
                if (it == node_by_uuid.end() || b.target == BindTarget::Unknown) {
                    continue;
                }
                const std::size_t node_index = it->second;
                const std::array<Corner, 4> corners =
                    sample_corners(xloc, yloc, ny, b.interpolate);

                if (b.target == BindTarget::Deform) {
                    if (b.deform_values.empty()) {
                        continue;
                    }
                    accumulate_deform(
                        b, corners,
                        puppet.nodes[node_index].mesh.vertex_count(),
                        out.vertex_offsets[node_index]);
                }
                else if (!b.scalar_values.empty()) {
                    accumulate_scalar(
                        b, corners, out.transform_deltas[node_index]);
                }
            }
        }

        return out;
    }
}
