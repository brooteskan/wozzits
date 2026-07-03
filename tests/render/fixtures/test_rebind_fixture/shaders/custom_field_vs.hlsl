// Custom-renderable fixture VS (issue #229): pull-mesh vertex shader against
// the AUTHORED binding layout (graph node 15) — a 20-dword root-constant
// block (Mvp16 head + a declared float4 "tint" tail) at b0 space2 and the two
// mesh-pull StructuredBuffers at t0/t1; registers all derived from row order.
cbuffer CustomBlock : register(b0, space2)
{
    float4x4 mvp;
    float4   tint;
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    uint   idx = indices[vid];
    float3 p   = positions[idx];

    VSOut o;
    o.pos = mul(mvp, float4(p, 1.0f));
    o.uv  = p.xy * 0.5f + 0.5f;
    return o;
}
