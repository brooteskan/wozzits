struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    float3 light_dir = normalize(float3(0.35f, 0.8f, 0.45f));
    float diffuse = saturate(dot(n, light_dir)) * 0.75f + 0.25f;

    float3 low = float3(0.20f, 0.34f, 0.18f);
    float3 high = float3(0.54f, 0.50f, 0.36f);
    float3 base = lerp(low, high, saturate(input.uv.y));

    return float4(base * diffuse, 1.0f);
}
