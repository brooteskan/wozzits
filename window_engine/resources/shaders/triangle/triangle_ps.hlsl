struct PSIn
{
    float4 pos : SV_POSITION; // must be here even if unused
    float3 world_pos : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    return float4(
        abs(input.world_pos.x),
        abs(input.world_pos.y),
        abs(input.world_pos.z) * 0.1,
        1.0
                );
}