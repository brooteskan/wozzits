StructuredBuffer<float3> Positions : register(t0);
StructuredBuffer<uint> Indices : register(t1);
StructuredBuffer<uint> PreferredNeighbors : register(t2);
StructuredBuffer<float> VertexWeights : register(t3);

RWStructuredBuffer<float3> OutPositions : register(u0);
RWStructuredBuffer<uint> OutIndices : register(u1);
RWStructuredBuffer<uint> OutCounter : register(u2);

cbuffer Constants : register(b0)
{
    uint VertexCount;
    uint TriangleCount;
    uint MaxIndexCount;
    uint UseWeights;
};

bool weight_allows(uint a, uint b)
{
    if (UseWeights == 0) {
        return true;
    }
    return VertexWeights[a] > 0.0f && VertexWeights[b] > 0.0f;
}

bool mutual_pair(uint v, out uint partner)
{
    partner = PreferredNeighbors[v];
    if (partner >= VertexCount || partner == v) {
        return false;
    }
    if (!weight_allows(v, partner)) {
        return false;
    }
    return PreferredNeighbors[partner] == v;
}

uint representative(uint v)
{
    uint partner = 0;
    if (!mutual_pair(v, partner)) {
        return v;
    }
    return min(v, partner);
}

[numthreads(128, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    const uint id = dispatch_id.x;

    if (id < VertexCount) {
        uint partner = 0;
        if (mutual_pair(id, partner)) {
            OutPositions[id] = (Positions[id] + Positions[partner]) * 0.5f;
        }
        else {
            OutPositions[id] = Positions[id];
        }
    }

    if (id < TriangleCount) {
        const uint i0 = Indices[id * 3u + 0u];
        const uint i1 = Indices[id * 3u + 1u];
        const uint i2 = Indices[id * 3u + 2u];
        const uint r0 = representative(i0);
        const uint r1 = representative(i1);
        const uint r2 = representative(i2);
        if (r0 == r1 || r1 == r2 || r2 == r0) {
            return;
        }

        uint base = 0;
        InterlockedAdd(OutCounter[0], 3u, base);
        if (base + 2u >= MaxIndexCount) {
            return;
        }
        OutIndices[base + 0u] = r0;
        OutIndices[base + 1u] = r1;
        OutIndices[base + 2u] = r2;
    }
}
