cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;
    float4 surface_color;
    float4 mask_params;
    float4 unmatched_color;
    float4 surface_params;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 world_pos = mul(world, float4(input.position, 1.0f));
    output.position = mul(view_proj, world_pos);
    output.normal = normalize(mul((float3x3)world, input.normal));
    return output;
}
