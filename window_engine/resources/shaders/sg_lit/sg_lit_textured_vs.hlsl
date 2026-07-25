// resources/shaders/sg_lit/sg_lit_textured_vs.hlsl
//
// Textured variant of sg_lit_vs: identical transform + world-normal work, plus
// the OBJECT-space normal passed through to the pixel shader. The PS derives a
// UV from that object normal (equirectangular), which is how a sphere gets a
// material texture without any UV channel in the mesh -- the pull path binds
// positions/indices/normals only, no UVs.
//
// Bindings (space2): t0 positions, t1 indices, t2 normals; the PS adds
// t3 sky_gaussian, t4 sky_gaussian_points, t5 material_albedo + s0 sampler.

cbuffer LitConstants : register(b0, space2)
{
    float4x4 world;               // c0..c3
    float4x4 view_proj;           // c4..c7
    float4   camera_and_diameter; // c8   (xyz = camera world pos)
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);
StructuredBuffer<float3> normals   : register(t2, space2);

struct VSOut
{
    float4 pos       : SV_POSITION;
    float3 world_pos : WORLDPOS;
    float3 world_nrm : NORMAL;
    float3 obj_nrm   : OBJECTNORMAL;   // UV derivation (material space)
};

VSOut main(uint vid : SV_VertexID)
{
    uint   i = indices[vid];
    float3 p = positions[i];
    float3 n = normals[i];

    float3 wp = mul(world, float4(p, 1.0f)).xyz;

    VSOut o;
    o.world_pos = wp;
    // Uniform scale assumed (no inverse-transpose needed); PS renormalizes.
    o.world_nrm = mul((float3x3)world, n);
    // Object space: the material stays pinned to the mesh, so the texture does
    // not swim when the node rotates.
    o.obj_nrm   = n;
    o.pos       = mul(view_proj, float4(wp, 1.0f));
    return o;
}
