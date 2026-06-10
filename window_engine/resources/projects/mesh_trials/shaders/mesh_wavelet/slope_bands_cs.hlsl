// Experimental variant with stronger slope bands.
//
// To try this variant, copy it over detail_heat_cs.hlsl or extend the scene
// materializer to select project-local wavelet kernels by function name.

struct VertexSignal
{
    float3 position;
    float3 normal;
};

StructuredBuffer<VertexSignal> Vertices : register(t0);
RWStructuredBuffer<float> Values : register(u0);

cbuffer Params : register(b0)
{
    uint VertexCount;
    uint ScaleCount;
    float LambdaMax;
    float Gamma;
    float3 BoundsMin;
    float BoundsRangeY;
    float3 BoundsMax;
    uint _Pad0;
};

float saturate01(float v)
{
    return min(max(v, 0.0), 1.0);
}

[numthreads(128, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint vertex_id = id.x;
    if (vertex_id >= VertexCount) {
        return;
    }

    const VertexSignal signal = Vertices[vertex_id];
    const float3 n = normalize(signal.normal);
    const float height01 = saturate01(
        (signal.position.y - BoundsMin.y) / max(BoundsRangeY, 0.0001));
    const float slope = saturate01(1.0 - abs(n.y));
    float detail_sum = 0.0;

    [loop]
    for (uint scale = 0; scale < ScaleCount; ++scale) {
        const float scale01 =
            (float(scale) + 1.0) / max(float(ScaleCount), 1.0);
        const float bands =
            0.5 + 0.5 * sin(height01 * max(LambdaMax, 0.0001)
                * (float(scale) + 1.0) * 6.28318530718);
        const float position_energy =
            pow(saturate01(height01 * bands), max(Gamma, 0.0001));
        const float normal_energy =
            pow(saturate01(slope * (0.2 + 0.8 * bands) * scale01),
                max(Gamma, 0.0001));

        Values[scale * 2u * VertexCount + vertex_id] = position_energy;
        Values[(scale * 2u + 1u) * VertexCount + vertex_id] = normal_energy;
        detail_sum += 0.35 * position_energy + 0.65 * normal_energy;
    }

    Values[(ScaleCount * 2u) * VertexCount + vertex_id] =
        detail_sum / max(float(ScaleCount), 1.0);
}
