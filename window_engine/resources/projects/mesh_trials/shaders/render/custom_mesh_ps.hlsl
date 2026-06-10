struct PSIn
{
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float field_value : TEXCOORD1;
};

float4 main(PSIn input) : SV_Target
{
    const float field = saturate(input.field_value);
    const float slope = saturate(1.0 - abs(normalize(input.normal).y));

    const float3 low = float3(0.05, 0.10, 0.16);
    const float3 mid = float3(0.18, 0.55, 0.34);
    const float3 high = float3(0.93, 0.82, 0.46);
    const float3 ridge = float3(0.95, 0.96, 0.90);

    float3 color = lerp(low, mid, smoothstep(0.05, 0.55, field));
    color = lerp(color, high, smoothstep(0.45, 0.82, field));
    color = lerp(color, ridge, smoothstep(0.72, 1.0, field) * slope);

    const float shade = saturate(input.normal.y * 0.35 + 0.75);
    return float4(color * shade, 1.0);
}
