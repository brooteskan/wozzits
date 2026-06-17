cbuffer Transform : register(b0, space2)
{
    float4x4 mvp;
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint> indices : register(t1, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
};

VSOut main(uint vid : SV_VertexID)
{
    uint idx = indices[vid];
    float3 p = positions[idx];

    VSOut o;
    o.pos = mul(mvp, float4(p, 1.0));
    return o;
}
