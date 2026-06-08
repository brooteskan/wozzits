cbuffer Transform : register(b0)
{
    float4x4 world;
    float4x4 view_proj;
    float4 style_color;
    float4 style_params;
};

StructuredBuffer<float> field_values : register(t0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint vertex_id : SV_VertexID;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float value : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 world_pos = mul(world, float4(input.position, 1.0f));
    output.position = mul(view_proj, world_pos);
    output.normal = normalize(mul((float3x3)world, input.normal));
    output.value = field_values[input.vertex_id];
    return output;
}
