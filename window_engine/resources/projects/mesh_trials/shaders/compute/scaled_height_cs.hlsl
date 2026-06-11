StructuredBuffer<float3> Positions : register(t0);
RWStructuredBuffer<float> Output : register(u0);

cbuffer Constants : register(b0)
{
    uint VertexCount;
    uint IndexCount;
    uint TriangleCount;
    uint ScaleBits;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= VertexCount) {
        return;
    }

    const float scale = asfloat(ScaleBits);
    const float y = Positions[id.x].y * scale;
    Output[id.x] = saturate(y);
}
