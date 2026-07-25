// src/engine/assets/inochi/puppet_draw_list.cpp
//
// Flatten a loaded Puppet into a static, draw-ordered list of Parts. Pure CPU:
// accumulate the 2D node-transform chain and zsort top-down from the root, then
// emit each enabled drawable Part with its placement affine, interleaved pos+uv
// vertices, indices, atlas page, opacity, and blend. See puppet_draw_list.h.

#include <engine/assets/inochi/puppet_draw_list.h>

#include <engine/assets/inochi/puppet_deform.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace wz::engine::assets::inochi
{
    Affine2D compose(const Affine2D& lhs, const Affine2D& rhs) noexcept
    {
        // result = lhs * rhs (apply rhs, then lhs).
        Affine2D r;
        r.a  = lhs.a * rhs.a + lhs.b * rhs.c;
        r.b  = lhs.a * rhs.b + lhs.b * rhs.d;
        r.tx = lhs.a * rhs.tx + lhs.b * rhs.ty + lhs.tx;
        r.c  = lhs.c * rhs.a + lhs.d * rhs.c;
        r.d  = lhs.c * rhs.b + lhs.d * rhs.d;
        r.ty = lhs.c * rhs.tx + lhs.d * rhs.ty + lhs.ty;
        return r;
    }

    std::array<float, 2> apply(const Affine2D& m, float x, float y) noexcept
    {
        return { m.a * x + m.b * y + m.tx, m.c * x + m.d * y + m.ty };
    }

    namespace
    {
        // A node's own local 2D transform: translate * rotateZ * scale. Inochi
        // stores rotation in radians (rot.z is the 2D spin) and a 2D scale.
        Affine2D local_affine(const Transform& t) noexcept
        {
            const float theta = t.rot[2];
            const float ct = std::cos(theta);
            const float st = std::sin(theta);
            const float sx = t.scale[0];
            const float sy = t.scale[1];
            Affine2D m;
            m.a = ct * sx; m.b = -st * sy; m.tx = t.trans[0];
            m.c = st * sx; m.d =  ct * sy; m.ty = t.trans[1];
            return m;
        }

        // The node's rest transform plus its deform delta (added trans/rot/scale).
        // Falls back to the rest transform when there is no deform for this node.
        Transform effective_transform(
            const Node& node, std::size_t i, const PuppetDeform* deform) noexcept
        {
            Transform t = node.transform;
            if (deform && i < deform->transform_deltas.size()) {
                const TransformDelta& d = deform->transform_deltas[i];
                t.trans[0] += d.trans[0];
                t.trans[1] += d.trans[1];
                t.trans[2] += d.trans[2];
                t.rot[0] += d.rot[0];
                t.rot[1] += d.rot[1];
                t.rot[2] += d.rot[2];
                t.scale[0] += d.scale[0];
                t.scale[1] += d.scale[1];
            }
            return t;
        }

        // The node's rest zsort plus its deform zsort delta.
        float effective_zsort(
            const Node& node, std::size_t i, const PuppetDeform* deform) noexcept
        {
            float z = node.zsort;
            if (deform && i < deform->transform_deltas.size()) {
                z += deform->transform_deltas[i].zsort;
            }
            return z;
        }
    } // namespace

    PuppetNodeTransforms compute_node_world_transforms(
        const Puppet& puppet, const PuppetDeform* deform)
    {
        PuppetNodeTransforms out;
        const std::size_t n = puppet.nodes.size();
        out.world.assign(n, Affine2D{});
        out.zacc.assign(n, 0.0f);
        if (n == 0) {
            return out;
        }

        // Accumulate world transform + zsort top-down. children are indices into
        // puppet.nodes; the root is nodes[0]. An iterative DFS keeps parent-before-
        // child ordering without assuming the flatten order is topological, and the
        // `visited` guard is defensive against malformed indices / cycles.
        std::vector<std::uint8_t> visited(n, 0);
        std::vector<std::size_t> stack;
        stack.reserve(n);
        out.world[0] = local_affine(effective_transform(puppet.nodes[0], 0, deform));
        out.zacc[0] = effective_zsort(puppet.nodes[0], 0, deform);
        visited[0] = 1;
        stack.push_back(0);
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            for (const std::size_t child : puppet.nodes[i].children) {
                if (child >= n || visited[child]) {
                    continue;
                }
                out.world[child] = compose(
                    out.world[i],
                    local_affine(
                        effective_transform(puppet.nodes[child], child, deform)));
                out.zacc[child] =
                    out.zacc[i] + effective_zsort(puppet.nodes[child], child, deform);
                visited[child] = 1;
                stack.push_back(child);
            }
        }
        return out;
    }

    PuppetDrawList build_puppet_draw_list(
        const Puppet& puppet, const PuppetDeform* deform)
    {
        PuppetDrawList out;
        const std::size_t n = puppet.nodes.size();
        if (n == 0) {
            return out;
        }

        const PuppetNodeTransforms xf =
            compute_node_world_transforms(puppet, deform);
        const std::vector<Affine2D>& world = xf.world;
        const std::vector<float>& zacc = xf.zacc;

        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        bool any_bounds = false;

        for (std::size_t i = 0; i < n; ++i) {
            const Node& node = puppet.nodes[i];
            if (node.kind != NodeKind::Part || !node.enabled) {
                continue;
            }
            const std::size_t vcount = node.mesh.vertex_count();
            if (vcount == 0 || node.mesh.indices.empty()) {
                continue;
            }

            PuppetPartDraw part;
            part.node_index = i;
            part.placement = world[i];
            part.opacity = node.opacity;
            part.blend = node.blend_mode;
            part.zsort = zacc[i];
            part.atlas_texture = node.textures.empty() ? 0u : node.textures[0];

            // Per-Part deform offsets (S3), added to the rest verts in Part-local
            // pixel space before placement. Empty / absent => rest positions.
            const std::vector<std::array<float, 2>>* offsets =
                (deform && i < deform->vertex_offsets.size()
                 && !deform->vertex_offsets[i].empty())
                    ? &deform->vertex_offsets[i]
                    : nullptr;

            part.vertices.resize(vcount);
            for (std::size_t vi = 0; vi < vcount; ++vi) {
                PuppetVertex& pv = part.vertices[vi];
                pv.px = node.mesh.verts[vi * 2 + 0];
                pv.py = node.mesh.verts[vi * 2 + 1];
                if (offsets && vi < offsets->size()) {
                    pv.px += (*offsets)[vi][0];
                    pv.py += (*offsets)[vi][1];
                }
                if (vi * 2 + 1 < node.mesh.uvs.size()) {
                    pv.u = node.mesh.uvs[vi * 2 + 0];
                    pv.v = node.mesh.uvs[vi * 2 + 1];
                }
                const std::array<float, 2> p = apply(world[i], pv.px, pv.py);
                min_x = std::min(min_x, p[0]);
                min_y = std::min(min_y, p[1]);
                max_x = std::max(max_x, p[0]);
                max_y = std::max(max_y, p[1]);
                any_bounds = true;
            }
            part.indices = node.mesh.indices;
            out.parts.push_back(std::move(part));
        }

        // Back-to-front by accumulated zsort. Inochi draws HIGHER zsort FIRST
        // (further back) and lower zsort last (in front), so sort DESCENDING.
        // (An earlier ascending sort rendered the puppet inside-out -- hair over
        // the face, tail in front of the body; #279.) Stable so equal-zsort Parts
        // keep tree order (Inochi's tie-break).
        std::stable_sort(
            out.parts.begin(), out.parts.end(),
            [](const PuppetPartDraw& lhs, const PuppetPartDraw& rhs) {
                return lhs.zsort > rhs.zsort;
            });

        if (any_bounds) {
            out.bounds_min = { min_x, min_y };
            out.bounds_max = { max_x, max_y };
        }
        return out;
    }
}
