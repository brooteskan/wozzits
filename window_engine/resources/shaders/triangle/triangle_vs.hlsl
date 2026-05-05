cbuffer Transform : register(b0)
{
    column_major float4x4 world;
    column_major float4x4 view_proj;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 world_pos : TEXCOORD0;
};

VSOut main(float3 pos : POSITION)
{
    VSOut o;
    float4 p = mul(world, float4(pos, 1.0));
    o.world_pos = p.xyz;
    o.pos = mul(view_proj, p);
    return o;
}