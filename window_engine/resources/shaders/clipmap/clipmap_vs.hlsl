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
//                                                  w=view_snapped flag)
//     80      16   float4   world_to_uv          (xy=scale, zw=offset)
//     96      16   float4   texel_and_vertical   (xy=texel_world_size,
//                                                  z=vertical_scale,
//                                                  w=base_height)
//    112      16   float4   texel_dims_extent    (xy=heightmap texel dims as
//                                                  float, z=base_resolution,
//                                                  w=reserved)
//   ------
//    128 bytes total = 32 dwords.

cbuffer Clipmap : register(b0, space2)
{
    float4x4 view_projection;
    float4   snap_params;         // xy = camera world XZ, z = c0, w = snapped?
    float4   world_to_uv;         // xy = uv scale, zw = uv offset
    float4   texel_and_vertical;  // xy = texel world size, z = vscale, w = base
    float4   texel_dims_extent;   // xy = texel dims (float), z = m, w = reserved
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
};

// Sample the heightmap at a world XZ position by BILINEAR filtering (#211,
// was point Load). uv is in [0,1] over the heightmap footprint; the sampler's
// clamp addressing samples the border texel for vertices that reach past the
// footprint edge (replacing the old manual texel clamp), so no wrap/error.
float sample_height_world(float2 world_xz)
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
    float2 dims       = max(texel_dims_extent.xy, 1.0f);
    float2 sample_uv  = uv + 0.5f / dims;
    return heightTex.SampleLevel(linearSampler, sample_uv, 0.0f);
}

// Bilinear interpolation of the height field on the coarser level's grid
// (spacing two_cL = 2*c_L). This yields the COARSE mesh's surface height at
// world_xz -- the linear blend between coarse vertices -- which is the correct
// geomorph target: a fully-morphed finer boundary vertex then lands exactly on
// the coarse edge (crack-free). A nearest coarse-vertex sample (round) would
// instead drop it onto a coarse vertex, leaving a T-junction gap on slopes.
float sample_height_coarse(float2 world_xz, float two_cL)
{
    float2 cg = world_xz / two_cL;
    float2 cf = floor(cg);
    float2 fr = cg - cf;
    float h00 = sample_height_world((cf + float2(0.0f, 0.0f)) * two_cL);
    float h10 = sample_height_world((cf + float2(1.0f, 0.0f)) * two_cL);
    float h01 = sample_height_world((cf + float2(0.0f, 1.0f)) * two_cL);
    float h11 = sample_height_world((cf + float2(1.0f, 1.0f)) * two_cL);
    return lerp(lerp(h00, h10, fr.x), lerp(h01, h11, fr.x), fr.y);
}

VSOut main(uint vid : SV_VertexID)
{
    uint   idx = indices[vid];
    float3 g   = positions[idx];

    float2 camera_xz   = snap_params.xy;
    float  c0          = snap_params.z;             // finest cell world size
    bool   view_snapped = snap_params.w > 0.5f;

    float vertical_scale = texel_and_vertical.z;
    float base_height    = texel_and_vertical.w;

    float2 world_xz;
    float  height;

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

        // POSITION geomorph (interior trim): each level snaps to its OWN grid,
        // so two adjacent levels can land up to one coarse cell apart, opening a
        // thin gap at the seam on the short side. Absorb that offset in a single
        // ONE-COARSE-CELL (2*c_L) -wide trim ring at this level's outer edge:
        // a_trim blends the snap from this level's grid to the next-coarser one
        // ONLY across that ring. The rest of the level stays RIGID (translates
        // whole with T_fine — no internal inch-worm); just the thin outer ring
        // stretches/collapses to keep the boundary closed as the snaps drift.
        float  a_trim   = saturate((dist - (half_world - twoCL)) / twoCL);
        float2 T_coarse = floor(camera_xz / fourCL) * fourCL;
        float2 T        = lerp(T_fine, T_coarse, a_trim);
        // g.xz are world offsets from the lattice center (world-sized mesh), so
        // place them directly — only the per-level snap T scales by the grid. c0
        // is still used above for the snap quantum (cL = exp2(level)*c0), just no
        // longer to scale g.
        world_xz        = T + g.xz;

        // HEIGHT geomorph: blend this level's per-vertex height to the COARSE
        // mesh's surface height (bilinear on the 2*c_L grid) over the same band,
        // so finer boundary vertices that fall between coarse vertices land on
        // the coarse edge (no T-junction, no pop).
        float h_fine   = sample_height_world(world_xz);
        float h_coarse = sample_height_coarse(world_xz, twoCL);
        height = lerp(h_fine, h_coarse, a);
    }
    else
    {
        // Non-view-snapped: the supplied mesh is already in world space and g.y
        // is real geometry — ignore the level tag, no per-level snap, no morph.
        world_xz = g.xz;
        height   = sample_height_world(world_xz);
    }

    float world_y = base_height + vertical_scale * height;

    // Finite-difference normal from neighbor height taps, one texel apart in
    // world space, so the PS can do simple lambertian-style shading. dh/dx and
    // dh/dz are scaled by vertical_scale to match the displaced surface. The
    // taps read the (un-morphed) field gradient at the morphed vertex position;
    // good enough for shading, and avoids re-deriving the morph per tap.
    float2 step_xz = texel_and_vertical.xy;  // one texel in world units
    float hx0 = sample_height_world(world_xz - float2(step_xz.x, 0.0f));
    float hx1 = sample_height_world(world_xz + float2(step_xz.x, 0.0f));
    float hz0 = sample_height_world(world_xz - float2(0.0f, step_xz.y));
    float hz1 = sample_height_world(world_xz + float2(0.0f, step_xz.y));
    float dhdx = vertical_scale * (hx1 - hx0) / max(2.0f * step_xz.x, 1e-6f);
    float dhdz = vertical_scale * (hz1 - hz0) / max(2.0f * step_xz.y, 1e-6f);
    float3 normal = normalize(float3(-dhdx, 1.0f, -dhdz));

    float3 world_pos = float3(world_xz.x, world_y, world_xz.y);

    VSOut o;
    o.pos       = mul(view_projection, float4(world_pos, 1.0f));
    o.world_pos = world_pos;
    o.normal    = normal;
    return o;
}
