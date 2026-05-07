struct PSIn
{
    float4 pos : SV_POSITION;
    float3 world_pos : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    return float4(0.9f, 0.7f, 0.2f, 1.0f);
}