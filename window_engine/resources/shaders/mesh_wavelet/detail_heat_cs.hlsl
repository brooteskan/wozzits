// shaders/mesh_wavelet/detail_heat_cs.hlsl

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

float apply_gamma(float v)
{
    return pow(saturate01(v), max(Gamma, 0.0001));
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
    const float terrain_signal = saturate01(
        0.65 * height01 + 0.35 * slope);
    float detail_sum = 0.0;

    [loop]
    for (uint scale = 0; scale < ScaleCount; ++scale) {
        const float scale_weight =
            (float(scale) + 1.0) / max(float(ScaleCount), 1.0);
        const float frequency =
            max(LambdaMax, 0.0001) * (float(scale) + 1.0);
        const float wave =
            0.5 + 0.5 * sin(terrain_signal * frequency * 6.28318530718);
        const float position_energy = apply_gamma(
            abs(height01 - 0.5) * 2.0 * (0.25 + 0.75 * wave));
        const float normal_energy = apply_gamma(
            slope * (0.35 + 0.65 * scale_weight) * (0.35 + 0.65 * wave));

        Values[scale * 2u * VertexCount + vertex_id] = position_energy;
        Values[(scale * 2u + 1u) * VertexCount + vertex_id] = normal_energy;
        detail_sum += 0.5 * position_energy + 0.5 * normal_energy;
    }

    Values[(ScaleCount * 2u) * VertexCount + vertex_id] =
        detail_sum / max(float(ScaleCount), 1.0);
}
