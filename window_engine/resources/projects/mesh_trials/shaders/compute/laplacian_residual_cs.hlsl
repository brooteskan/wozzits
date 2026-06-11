// Laplacian residual over the prebuilt sparse mesh operator (#150/#151).
//
// The operator is a cached CSR asset; this kernel does only the sparse
// apply. NeighborWeights convention:
//   smooth[i]   = sum_j w_ij * x[j]
//   residual[i] = x[i] - smooth[i]
// (The y[i] = sum_j w_ij * (x[i] - x[j]) form is equivalent only because
// uniform rows sum to 1.) Rows with no neighbors output zero detail —
// an isolated vertex is not detail. The engine-filled metadata constants
// let the kernel refuse an operator it was not written for.

StructuredBuffer<uint>   RowOffsets : register(t0);
StructuredBuffer<uint>   ColIndices : register(t1);
StructuredBuffer<float>  Weights    : register(t2);
StructuredBuffer<float>  VertexMass : register(t3);
StructuredBuffer<float3> Positions  : register(t4);
RWStructuredBuffer<float> Output    : register(u0);

cbuffer Constants : register(b0)
{
    uint RowCount;
    uint NonzeroCount;
    uint OperatorKind;      // 0 = uniform vertex Laplacian
    uint ValueConvention;   // 0 = NeighborWeights
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= RowCount) {
        return;
    }
    if (OperatorKind != 0u || ValueConvention != 0u) {
        Output[id.x] = 0.0;
        return;
    }

    uint row_begin = RowOffsets[id.x];
    uint row_end = RowOffsets[id.x + 1u];
    if (row_begin == row_end) {
        Output[id.x] = 0.0;
        return;
    }

    float3 smooth = float3(0.0, 0.0, 0.0);
    for (uint e = row_begin; e < row_end; ++e) {
        smooth += Weights[e] * Positions[ColIndices[e]];
    }
    float3 residual = Positions[id.x] - smooth;
    Output[id.x] = length(residual) / VertexMass[id.x];
}
