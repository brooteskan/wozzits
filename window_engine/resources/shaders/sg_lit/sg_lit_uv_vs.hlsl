// resources/shaders/sg_lit/sg_lit_uv_vs.hlsl
//
// UV-mapped variant of sg_lit_vs: identical transform + world-normal work, plus
// the mesh's OWN texture coordinates pulled through pulled_mesh_uvs (#290) and
// passed to the pixel shader.
//
// This is the variant for geometry that HAS a UV channel. sg_lit_textured_*
// derives an equirectangular UV from the object normal instead, which is right
// for a sphere (an icosphere has no non-degenerate unwrap) and meaningless on
// anything else -- that was the whole of #290's complaint.
//
// Bindings (space2): t0 positions, t1 indices, t2 normals, t6 uvs. The sky rows
// at t3/t4 belong to the PS via sg_lighting_common; the UV row is appended at t6
// so t0..t5 stay identical to the sg_lit_textured layout.

cbuffer LitConstants : register(b0, space2)
{
    float4x4 world;               // c0..c3
    float4x4 view_proj;           // c4..c7
    float4   camera_and_diameter; // c8   (xyz = camera world pos)
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);
StructuredBuffer<float3> normals   : register(t2, space2);
StructuredBuffer<float2> uvs       : register(t6, space2);

struct VSOut
{
    float4 pos       : SV_POSITION;
    float3 world_pos : WORLDPOS;
    float3 world_nrm : NORMAL;
    float2 uv        : TEXCOORD0;
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
    o.uv        = uvs[i];
    o.pos       = mul(view_proj, float4(wp, 1.0f));
    return o;
}
