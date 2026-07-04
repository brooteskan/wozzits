// Custom-renderable fixture VS BODY (issues #229/#231): no hand-declared
// cbuffer/SRV/sampler headers — the binding-prelude node (graph node 18)
// prepends the declarations generated from the authored layout (node 15), so
// this body only references them by their semantic names (mvp, tint,
// pulled_mesh_positions, pulled_mesh_indices). Declaring registers here would
// re-create exactly the drift #231 removes.

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
    o.uv  = p.xy * 0.5f + 0.5f;
    return o;
}
