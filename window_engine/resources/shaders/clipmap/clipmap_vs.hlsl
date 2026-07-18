// resources/shaders/clipmap/clipmap_vs.hlsl
//
// Geometry-clipmap landscape vertex shader. It pulls the static lattice mesh
// (kProceduralClipmapLatticeMeshSchema, WORLD-SIZED in XZ centered at the
// origin — the compiler bakes the finest cell metres into the positions — with
// each vertex's LOD level in position.y) from a StructuredBuffer, places each
// LOD level in world space and snaps it INDEPENDENTLY to a multiple of that
// level's own cell (issue #207), samples the resident R32 height texture (#197)
// by integer texel fetch, vertically geomorphs the LOD seams, displaces world
// Y, and projects. Because the mesh is world-sized, g.xz are world offsets and
// are placed directly; c0 (snap_params.z) is the finest cell world size, used
// only for the per-level snap quantum (c_L = 2^L * c0), not to scale g.
//
// Per-level snap (the fix for coarse-ring lurch): for a vertex at local grid xz
// with level L, c_L = 2^L * c0 and T_L = floor(camera_xz/(2*c_L))*(2*c_L), so
// each level's grid stays put on its own grid as the camera moves. The 2x makes
// the levels nest (adjacent boundaries stay spatially coincident); the seam is
// then closed by a vertical geomorph that lerps the finer level's edge height
// to the coarser level's, so the finer boundary vertices land on the coarse
// edge — no T-junction, no pop. A single whole-lattice snap (the old path) kept
// only levels 0-1 aligned and let every coarser ring lurch sub-cell.
//
// view_snapped flag (snap_params.w): 1 for the procedural lattice (position.y
// is a LOD level — do the per-level snap). 0 for an arbitrary supplied static
// mesh (#205): position.y is real geometry, so skip all per-level logic and
// place the mesh in world space unchanged (identity transform).
//
// Object SRG = space2 (binding_layout == 2 in render_program_compilers.cpp).
//
// ── cbuffer byte layout (MUST match ClipmapDrawConstants in
//    src/engine/rendering/rhi_scene_renderer.cpp byte-for-byte) ───────────────
//
//   offset  bytes  field
//   ------  -----  -----------------------------------------------------------
//      0      64   float4x4 view_projection      (column-major, mul(VP,p))
//     64      16   float4   snap_params          (xy=camera world XZ, z=c0,
//                                                  w=ring count, 0=not a clipmap)
//     80      16   float4   world_to_uv          (xy=scale, zw=offset)
//     96      16   float4   texel_and_vertical   (xy=texel_world_size,
//                                                  z=vertical_scale,
//                                                  w=base_height)
//    112      16   float4   texel_dims_extent    (xy=heightmap texel dims as
//                                                  float, z=base_resolution,
//                                                  w=height mip count (#210))
//   ------
//    128 bytes total = 32 dwords.

cbuffer Clipmap : register(b0, space2)
{
    float4x4 view_projection;
    float4   snap_params;         // xy = camera world XZ, z = c0,
                                  // w = ring count (0 = not view-snapped)
    float4   world_to_uv;         // xy = uv scale, zw = uv offset
    float4   texel_and_vertical;  // xy = texel world size, z = vscale, w = base
    float4   texel_dims_extent;   // xy = texel dims (float), z = m,
                                  // w = height mip count (#210)
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);
Texture2D<float>         heightTex : register(t2, space2);

// Static linear-clamp sampler (baked into the root signature by the clipmap
// program's StaticSamplerBinding; see render_program_compilers.cpp
// binding_layout == 2). Bilinear-filters the height field so the surface no
// longer steps between texels (#211). Clamp addressing reproduces the old
// per-tap edge clamp for vertices that reach past the footprint.
SamplerState linearSampler : register(s0, space2);

// Geomorph band, as a fraction of each level's world half-extent (m/2)*c_L.
// The finer level's height blends to the coarser level's over its outer
// [MORPH_START, MORPH_END] fraction, so it has fully matched the coarse edge
// by the time its vertices reach the hand-off boundary. Tunable; the reviewer
// adjusts these for transition quality.
#define CLIPMAP_MORPH_START 0.80f
#define CLIPMAP_MORPH_END   0.98f

struct VSOut
{
    float4 pos        : SV_POSITION;
    float3 world_pos  : TEXCOORD0;  // displaced world position (for PS shading)
    float3 normal     : NORMAL;     // finite-difference height normal
    float3 bary       : TEXCOORD1;  // per-corner basis for a solid-fill wireframe
};

// Sample the heightmap at a world XZ position by BILINEAR filtering (#211, was
// point Load) from a chosen mip LEVEL (#210). uv is in [0,1] over the heightmap
// footprint; the sampler's clamp addressing samples the border texel for
// vertices that reach past the footprint edge (replacing the old manual texel
// clamp), so no wrap/error.
//
// Mip selection (#210): a level-k clipmap ring covers 2^k texels per lattice
// step, so point-sampling the full-res field there skips 2^k-1 texels and the
// far rings shimmer. Sampling mip k -- a 2^k box-filter of the field, built CPU
// -side and uploaded per level -- reads the averaged height for that footprint,
// anti-aliasing the coarse rings. mip is clamped by the caller to the resident
// chain length. Level 0 passes mip 0 and is bit-identical to the #211 path.
float sample_height_world(float2 world_xz, float mip)
{
    float2 uv = world_to_uv.xy * world_xz + world_to_uv.zw;

    // Texel-CELL convention: texel i covers uv [i/dims, (i+1)/dims), so the old
    // floor(uv*dims) point fetch returned texel i's value for any uv in that
    // cell -- in particular at the cell's LEFT edge uv = i/dims (where the
    // lattice vertices land, one cell = dims/extent = 4 texels). To reproduce
    // that value at the vertex AND interpolate linearly across the cell, sample
    // at the texel CENTER: hardware bilinear taps sit at (i+0.5)/dims, so shift
    // the footprint uv by +half a texel. Then uv = i/dims maps to texel i's
    // center -> returns exactly h[i] (matches the point path at the vertex), and
    // across the cell it blends h[i] -> h[i+1] -- the standard texel-center
    // bilinear the collision bicubic field tracks. The clamp addressing on the
    // sampler handles vertices that reach past the [0,1] footprint edge.
    //
    // The footprint uv is mip-INDEPENDENT (it maps world XZ to [0,1] over the
    // whole texture). Only the half-texel center shift scales with the mip,
    // since mip m's dimensions are dims >> m: shift by 0.5 / (dims >> m).
    float2 mip_dims   = max(floor(texel_dims_extent.xy / exp2(mip)), 1.0f);
    float2 sample_uv  = uv + 0.5f / mip_dims;
    return heightTex.SampleLevel(linearSampler, sample_uv, mip);
}

// The geomorph target: the coarser ring's (level L+1) TRIANGULATED surface
// height at world_xz. The L+1 ring is a mesh: at its vertices (spacing
// two_cL = 2*c_L = c_{L+1}) it samples the height at mip L+1, but BETWEEN
// vertices the rasterizer interpolates those vertex heights linearly along the
// triangle edge (the chord). So the correct target is the bilinear blend of the
// FOUR surrounding L+1 VERTEX samples -- linear-between-vertices -- and a fully
// -morphed level-L boundary vertex then lands exactly on the L+1 triangle edge
// (the same linear blend of the same two vertex samples): crack-free.
//
// #210 changes only WHAT each corner tap reads: mip L+1 (the box-filtered field
// the L+1 ring actually samples at its vertices) instead of the full-res field.
// The 4-tap structure must STAY: a single mip-(L+1) texture tap at world_xz is
// NOT equivalent, because an L+1 lattice cell spans ~4 mip-(L+1) texels
// (dims/extent), so the texture bilinear has knots at texel centers BETWEEN the
// vertices and differs from the chord on curved terrain -- that mismatch is a
// hairline T-junction crack at the seam.
//
// Corner uv math: each corner is an L+1 lattice vertex position, which lands on
// a texel-cell LEFT edge at mip L+1 (uv = i/mip_dims); sample_height_world's
// per-mip half-texel shift (#211/#210) maps that to texel i's center, returning
// exactly h[i] -- the same value the L+1 ring's own vertex sample produces,
// which is what makes the blend land on its edge.
float sample_height_coarse(float2 world_xz, float two_cL, float coarse_mip)
{
    float2 cg = world_xz / two_cL;
    float2 cf = floor(cg);
    float2 fr = cg - cf;
    float h00 = sample_height_world((cf + float2(0.0f, 0.0f)) * two_cL, coarse_mip);
    float h10 = sample_height_world((cf + float2(1.0f, 0.0f)) * two_cL, coarse_mip);
    float h01 = sample_height_world((cf + float2(0.0f, 1.0f)) * two_cL, coarse_mip);
    float h11 = sample_height_world((cf + float2(1.0f, 1.0f)) * two_cL, coarse_mip);
    return lerp(lerp(h00, h10, fr.x), lerp(h01, h11, fr.x), fr.y);
}

VSOut main(uint vid : SV_VertexID)
{
    uint   idx = indices[vid];
    float3 g   = positions[idx];

    float2 camera_xz   = snap_params.xy;
    float  c0          = snap_params.z;             // finest cell world size
    // Ring count, 0 for an arbitrary supplied static mesh (#205). The count --
    // not just "is this a clipmap" -- is what lets the outermost ring know it
    // is last; see the morph suppression below.
    float  level_count  = snap_params.w;
    bool   view_snapped = level_count > 0.5f;

    float vertical_scale = texel_and_vertical.z;
    float base_height    = texel_and_vertical.w;

    // #210: highest mip index available in the resident height chain (count-1).
    // Every sampled mip is clamped to this so a lattice with more LOD levels than
    // the texture has mips just reuses the coarsest box-filtered level.
    float max_mip = max(texel_dims_extent.w - 1.0f, 0.0f);

    float2 world_xz;
    float  height;
    float  height_mip;   // mip this vertex samples for shading-normal taps

    if (view_snapped)
    {
        // ── Per-level placement + position/height geomorph (issue #207) ─────
        // g.y carries the LOD level; c_L = 2^L * c0 is this level's world cell.
        float  level  = max(g.y, 0.0f);
        float  cL     = exp2(level) * c0;
        float  twoCL  = 2.0f * cL;          // this level's snap quantum (2*c_L)
        float  fourCL = 2.0f * twoCL;       // the next-coarser level's snap

        // This level's own snap (floor so the grid origin steps consistently).
        float2 T_fine = floor(camera_xz / twoCL) * twoCL;

        // Morph factor: 0 in the interior, ramping to 1 at this level's outer
        // edge. g.xz are now WORLD offsets from the lattice center (the lattice
        // mesh is world-sized — its positions bake the finest cell metres in), so
        // the vertex's OWN distance from the center is just |g.xz| (no c0 scale)
        // — computed before any morph so it can't feed back — and the band stays
        // fixed in the level's grid (no wobble as the camera moves within a snap
        // cell). base_resolution (m) is in extent.z; half_world stays m/2 * cL.
        float m          = max(texel_dims_extent.z, 1.0f);
        float half_world = 0.5f * m * cL;
        float dist       = max(abs(g.x), abs(g.z));
        float morph_start = CLIPMAP_MORPH_START * half_world;
        float morph_end   = CLIPMAP_MORPH_END * half_world;
        float a = saturate(
            (dist - morph_start) / max(morph_end - morph_start, 1e-6f));

        // The OUTERMOST ring has no coarser neighbour to hand off to, so it
        // must not morph toward one. Without the ring count the VS could not
        // tell, and blended this ring's outer band toward a level that is never
        // drawn: the far field was coarsened past its own mip for no reason,
        // and the trim below stretched the horizon edge as the camera crossed a
        // snap cell. Last ring => rigid to its edge, reading its own mip.
        bool is_outermost = (level + 1.5f) >= level_count;
        if (is_outermost) {
            a = 0.0f;
        }

        // POSITION geomorph (interior trim): each level snaps to its OWN grid,
        // so two adjacent levels can land up to one coarse cell apart, opening a
        // thin gap at the seam on the short side. Absorb that offset in a single
        // ONE-COARSE-CELL (2*c_L) -wide trim ring at this level's outer edge:
        // a_trim blends the snap from this level's grid to the next-coarser one
        // ONLY across that ring. The rest of the level stays RIGID (translates
        // whole with T_fine — no internal inch-worm); just the thin outer ring
        // stretches/collapses to keep the boundary closed as the snaps drift.
        float  a_trim   = is_outermost
            ? 0.0f
            : saturate((dist - (half_world - twoCL)) / twoCL);
        float2 T_coarse = floor(camera_xz / fourCL) * fourCL;
        float2 T        = lerp(T_fine, T_coarse, a_trim);
        // g.xz are world offsets from the lattice center (world-sized mesh), so
        // place them directly — only the per-level snap T scales by the grid. c0
        // is still used above for the snap quantum (cL = exp2(level)*c0), just no
        // longer to scale g.
        world_xz        = T + g.xz;

        // HEIGHT geomorph (#210): blend this level's box-filtered height (mip L)
        // to the coarser ring's TRIANGULATED surface (bilinear between the four
        // surrounding L+1 vertices, each sampled at mip L+1) over the same band,
        // so finer boundary vertices land on the L+1 triangle edge by the
        // hand-off (no T-junction, no pop). Both mips are clamped to the resident
        // chain; when L is already the coarsest mip the corner taps read the same
        // level as h_fine, but the 4-tap blend still targets the coarser GRID.
        float mip_fine   = min(level, max_mip);
        float mip_coarse = min(level + 1.0f, max_mip);
        height_mip       = mip_fine;
        float h_fine   = sample_height_world(world_xz, mip_fine);
        float h_coarse = sample_height_coarse(world_xz, twoCL, mip_coarse);
        height = lerp(h_fine, h_coarse, a);
    }
    else
    {
        // Non-view-snapped: the supplied mesh is already in world space and g.y
        // is real geometry — ignore the level tag, no per-level snap, no morph.
        // Sample the full-res field (mip 0), as before.
        world_xz   = g.xz;
        height_mip = 0.0f;
        height     = sample_height_world(world_xz, 0.0f);
    }

    float world_y = base_height + vertical_scale * height;

    // Finite-difference normal from neighbor height taps, one texel apart in
    // world space, so the PS can do simple lambertian-style shading. dh/dx and
    // dh/dz are scaled by vertical_scale to match the displaced surface. The
    // taps read the (un-morphed) field gradient at the morphed vertex position;
    // good enough for shading, and avoids re-deriving the morph per tap. They
    // sample this vertex's height_mip (#210) so a coarse ring's shading tracks
    // its box-filtered surface rather than the full-res field's fine gradient.
    float2 step_xz = texel_and_vertical.xy;  // one texel in world units
    float hx0 = sample_height_world(world_xz - float2(step_xz.x, 0.0f), height_mip);
    float hx1 = sample_height_world(world_xz + float2(step_xz.x, 0.0f), height_mip);
    float hz0 = sample_height_world(world_xz - float2(0.0f, step_xz.y), height_mip);
    float hz1 = sample_height_world(world_xz + float2(0.0f, step_xz.y), height_mip);
    float dhdx = vertical_scale * (hx1 - hx0) / max(2.0f * step_xz.x, 1e-6f);
    float dhdz = vertical_scale * (hz1 - hz0) / max(2.0f * step_xz.y, 1e-6f);
    float3 normal = normalize(float3(-dhdx, 1.0f, -dhdz));

    float3 world_pos = float3(world_xz.x, world_y, world_xz.y);

    VSOut o;
    o.pos       = mul(view_projection, float4(world_pos, 1.0f));
    o.world_pos = world_pos;
    o.normal    = normal;

    // Per-corner basis for a solid-fill wireframe PS. The clipmap is drawn
    // non-indexed (the VS fetches indices[vid] itself), so each triangle is
    // three CONSECUTIVE vertex ids and vid % 3 is the corner. The rasteriser
    // interpolates this to per-pixel barycentrics; clipmap_wire_ps reads it to
    // find the distance to the nearest triangle edge. The shaded clipmap_ps
    // does not declare this input, so it simply ignores the extra output.
    uint corner = vid % 3u;
    o.bary = float3(corner == 0u ? 1.0f : 0.0f,
                    corner == 1u ? 1.0f : 0.0f,
                    corner == 2u ? 1.0f : 0.0f);
    return o;
}
