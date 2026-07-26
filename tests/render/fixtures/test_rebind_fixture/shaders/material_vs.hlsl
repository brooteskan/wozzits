// Composite-material fixture VS BODY (#290). Sibling of custom_field_vs.hlsl,
// which derives its UV from the vertex position (p.xy * 0.5 + 0.5) because the
// pull path had no UV channel. This one pulls the mesh's OWN texture
// coordinates through pulled_mesh_uvs, so a material texture maps the way the
// mesh was authored rather than the way its geometry happens to be shaped.
//
// Declarations come from the binding-prelude node generated off layout node 21;
// this body only names them by semantic.

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    uint   idx = pulled_mesh_indices[vid];
    float3 p   = pulled_mesh_positions[idx];

    VSOut o;
    o.pos = mul(mvp, float4(p, 1.0f));
    o.uv  = pulled_mesh_uvs[idx];
    return o;
}
