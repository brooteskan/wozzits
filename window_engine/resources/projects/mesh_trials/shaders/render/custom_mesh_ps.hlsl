struct PSIn
{
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_Target
{
    float shade = saturate(input.normal.y * 0.5 + 0.5);
    return float4(input.uv.x, shade, input.uv.y, 1.0);
}