cbuffer MeshConstants : register(b0)
{
	float4x4 World;
	float4x4 ViewProj;
	float4 Style0;
	float4 Style1;
};

struct VSIn
{
	float3 pos : POSITION;
	float3 normal : NORMAL;
	float2 uv : TEXCOORD0;
};

struct VSOut
{
	float4 pos : SV_Position;
	float3 normal : NORMAL;
	float2 uv : TEXCOORD0;
};

VSOut main(VSIn input)
{
	VSOut output;
	float4 world_pos = mul(World, float4(input.pos, 1.0));
	output.pos = mul(ViewProj, world_pos);
	output.normal = input.normal;
	output.uv = input.uv;
	return output;
}