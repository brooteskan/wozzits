// Body only — the binding prelude declares mvp / pulled_mesh_positions /
// pulled_mesh_indices. Do NOT redeclare them here.
struct VSOut
{
    float4 pos : SV_POSITION;
};

VSOut main(uint vid : SV_VertexID)
{
    uint idx = pulled_mesh_indices[vid];
    float3 p = pulled_mesh_positions[idx];

    VSOut o;
    o.pos = mul(mvp, float4(p, 1.0));
    return o;
}