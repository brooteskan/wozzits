#pragma once

// engine/assets/inochi/puppet_draw_list.h
//
// The static-pose draw list for an Inochi2D puppet: its drawable Parts flattened
// to render-ready per-Part records -- a 2D affine placement (the Part's local
// pixel space -> the puppet's pixel space, from the accumulated node-transform
// chain), interleaved (pos,uv) vertex data + indices for a vertex-pull draw, the
// atlas page it samples, opacity, blend, and an accumulated zsort key for draw
// order. This is PURE (no GPU): the residency seam (puppet_gpu) uploads it, and
// the renderer composes a puppet->screen placement on top. The deform seam (S3)
// rebuilds the vertex data per frame from this same structure.

#include <engine/assets/inochi/inochi_puppet.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wz::engine::assets::inochi
{
    // The evaluated per-node transform deltas + per-Part vertex offsets a pose
    // contributes (see puppet_deform.h). build_puppet_draw_list applies it as an
    // optional argument; a pointer keeps the header free of the deform include.
    struct PuppetDeform;

    // A 2D affine packed as two rows: x' = a*x + b*y + tx ; y' = c*x + d*y + ty.
    struct Affine2D
    {
        float a = 1.0f, b = 0.0f, tx = 0.0f;
        float c = 0.0f, d = 1.0f, ty = 0.0f;
    };

    // Compose two affines. The result applies `rhs` first, then `lhs`
    // (i.e. lhs * rhs), matching parent-then-child transform accumulation.
    [[nodiscard]] Affine2D compose(const Affine2D& lhs, const Affine2D& rhs) noexcept;

    // Apply an affine to a point.
    [[nodiscard]] std::array<float, 2> apply(const Affine2D& m, float x, float y) noexcept;

    // One interleaved vertex for the puppet vertex-pull program: Part-local
    // pixel-space position + atlas-relative uv. Byte-compatible with the HLSL
    // WzPuppetVertex { float2 pos; float2 uv; } (16-byte structured-buffer stride).
    struct PuppetVertex
    {
        float px = 0.0f, py = 0.0f;  // Part-local pixel space
        float u = 0.0f, v = 0.0f;    // atlas-relative [0,1]
    };

    // One drawable Part, resolved against the static pose.
    struct PuppetPartDraw
    {
        std::size_t node_index = 0;             // index into Puppet::nodes
        std::vector<PuppetVertex> vertices;     // Part-local verts (pos+uv)
        std::vector<std::uint32_t> indices;     // 3*T
        std::uint32_t atlas_texture = 0;        // index into Puppet::textures (albedo page)
        Affine2D placement;                     // Part-local pixels -> puppet pixels
        float opacity = 1.0f;
        BlendMode blend = BlendMode::Normal;
        float zsort = 0.0f;                     // accumulated; draw-order key
    };

    // The flattened, draw-ordered Part list for a static puppet pose, plus the
    // puppet-space bounds (the union of placed Part verts) so the renderer can
    // center/scale the puppet on screen.
    struct PuppetDrawList
    {
        std::vector<PuppetPartDraw> parts;      // sorted back-to-front (ascending zsort)
        std::array<float, 2> bounds_min{ 0.0f, 0.0f };
        std::array<float, 2> bounds_max{ 0.0f, 0.0f };

        [[nodiscard]] bool empty() const noexcept { return parts.empty(); }
    };

    // Build the draw list: walk the node tree from the root (nodes[0]), accumulate
    // each node's 2D transform + zsort top-down, collect every enabled drawable
    // Part, interleave its pos+uv, and sort ascending by accumulated zsort (back to
    // front). Pure function of the puppet; no GPU. Disabled Parts and Parts with
    // empty meshes are skipped; Composites are flattened (their child Parts draw
    // inline -- the offscreen composite grouping is seam 6).
    //
    // When `deform` is non-null it is applied as it builds (seam S3): each node's
    // effective transform/zsort = base + the node's TransformDelta, and each Part
    // vertex is offset by its per-vertex deform before placement. `deform` is
    // indexed by node index (as evaluate_puppet_deform returns); a null pointer
    // yields the static rest pose (the previous behaviour).
    [[nodiscard]] PuppetDrawList build_puppet_draw_list(
        const Puppet& puppet, const PuppetDeform* deform = nullptr);
}
