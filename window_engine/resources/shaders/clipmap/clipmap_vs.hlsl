// resources/shaders/clipmap/clipmap_vs.hlsl
//
// Geometry-clipmap landscape vertex shader (issue #198 slice 3b). It pulls the
// static lattice mesh (kProceduralClipmapLatticeMeshSchema, authored unitless
// in XZ centered at the origin) from a StructuredBuffer, places it in world
// space via the per-frame camera-snapped transform, samples the resident R32
// height texture (#197) by integer texel fetch (no sampler — step 1 deferred
// them), displaces world Y, and projects.
//
// Object SRG = space2 (binding_layout == 2 in render_program_compilers.cpp).
//
// ── cbuffer byte layout (MUST match ClipmapDrawConstants in
//    src/engine/rendering/rhi_scene_renderer.cpp byte-for-byte) ───────────────
//
//   offset  bytes  field
//   ------  -----  -----------------------------------------------------------
//      0      64   float4x4 view_projection           (column-major, mul(VP,p))
//     64      16   float4   lattice_translation_scale (xyz=translation, w=scale)
//     80      16   float4   world_to_uv               (xy=scale, zw=offset)
//     96      16   float4   texel_and_vertical        (xy=texel_world_size,
//                                                       z=vertical_scale,
//                                                       w=base_height)
//    112      16   float4   texel_dims                (xy=heightmap texel dims
//                                                       as float, zw=reserved)
//   ------
//    128 bytes total = 32 dwords. Every float4 lands on a 16-byte boundary, so
//   the HLSL cbuffer packing and the tightly-packed C++ struct agree exactly.

cbuffer Clipmap : register(b0, space2)
{
    float4x4 view_projection;
    float4   lattice_translation_scale;  // xyz = translation (world), w = scale
    float4   world_to_uv;                // xy = uv scale, zw = uv offset
    float4   texel_and_vertical;         // xy = texel world size, z = vscale, w = base
    float4   texel_dims;                 // xy = texel dims (float), zw = reserved
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);
Texture2D<float>         heightTex : register(t2, space2);

struct VSOut
{
    float4 pos        : SV_POSITION;
    float3 world_pos  : TEXCOORD0;  // displaced world position (for PS shading)
    float3 normal     : NORMAL;     // finite-difference height normal
};

// Sample the heightmap at a world XZ position by integer texel fetch. uv is in
// [0,1] over the heightmap footprint; clamp so lattice vertices that reach past
// the footprint edge sample the border texel rather than wrapping/erroring.
float sample_height_world(float2 world_xz)
{
    float2 uv = world_to_uv.xy * world_xz + world_to_uv.zw;
    uv = clamp(uv, 0.0f, 1.0f);

    // texel_dims.xy is the heightmap's texel count; the last addressable texel
    // is dims-1. Load() takes integer texel coordinates (no filtering).
    float2 max_texel = max(texel_dims.xy - 1.0f, 0.0f);
    int2 texel = int2(round(uv * max_texel));
    return heightTex.Load(int3(texel, 0));
}

VSOut main(uint vid : SV_VertexID)
{
    uint   idx = indices[vid];
    float3 g   = positions[idx];

    // Lattice grid space -> world: uniform XZ scale + camera-snapped translate.
    float3 t    = lattice_translation_scale.xyz;
    float  s    = lattice_translation_scale.w;
    float2 world_xz = t.xz + s * g.xz;

    float vertical_scale = texel_and_vertical.z;
    float base_height    = texel_and_vertical.w;

    float h       = sample_height_world(world_xz);
    float world_y = base_height + vertical_scale * h;

    // Finite-difference normal from neighbor height taps, one texel apart in
    // world space, so the PS can do simple lambertian-style shading. dh/dx and
    // dh/dz are scaled by vertical_scale to match the displaced surface.
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
