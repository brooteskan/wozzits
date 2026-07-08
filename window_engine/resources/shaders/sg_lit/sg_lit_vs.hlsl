// resources/shaders/sg_lit/sg_lit_vs.hlsl
//
// SG-lit PBR surface (lighting model Seam 3, umbrella #259). The first mesh in
// the engine shaded by a real BRDF instead of the hardcoded half-Lambert -- the
// moment the engine becomes PBR. Lit by the SAME resident spherical-Gaussian
// lobe buffer the sky renders from ("sky_gaussian"): one SG set, two operators.
//
// Constants head = WorldViewProjCamera36 (world/view_proj/camera). The layout
// (0x104) binds pulled_mesh_positions -> t0, pulled_mesh_indices -> t1,
// pulled_mesh_normals -> t2 (Seam 3a's real resident normals buffer), and (in
// the PS) sky_gaussian -> t3, all in space2. We pull the real per-vertex normal
// and pass a world-space normal + world position to the PS.

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
    o.pos       = mul(view_proj, float4(wp, 1.0f));
    return o;
}
