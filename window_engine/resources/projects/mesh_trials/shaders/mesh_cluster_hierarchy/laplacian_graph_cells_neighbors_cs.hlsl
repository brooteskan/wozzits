StructuredBuffer<float3> Positions : register(t0);
StructuredBuffer<uint> RowOffsets : register(t1);
StructuredBuffer<uint> ColIndices : register(t2);
RWStructuredBuffer<uint> PreferredNeighbors : register(u0);

cbuffer Constants : register(b0)
{
    uint VertexCount;
    uint _Pad0;
    uint _Pad1;
    uint _Pad2;
};

[numthreads(128, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint v = id.x;
    if (v >= VertexCount) {
        return;
    }

    const uint begin = RowOffsets[v];
    const uint end = RowOffsets[v + 1];
    const float3 p = Positions[v];

    uint best = v;
    float best_len = 3.402823466e+38f;
    for (uint e = begin; e < end; ++e) {
        const uint n = ColIndices[e];
        if (n >= VertexCount || n == v) {
            continue;
        }

        const float3 d = Positions[n] - p;
        const float len = dot(d, d);
        if (len < best_len || (len == best_len && n < best)) {
            best_len = len;
            best = n;
        }
    }

    PreferredNeighbors[v] = best;
}
