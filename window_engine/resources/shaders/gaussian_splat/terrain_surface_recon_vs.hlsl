// shaders/gaussian_splat/terrain_surface_recon_vs.hlsl
//
// Vertex shader for the terrain surface reconstruction renderer.
//
// Receives pre-displaced world-space vertices from the CPU-side
// reconstruction grid.  Transforms to clip space and passes
// normal/color/weight through for the pixel shader.
//
// Root constants (24 dwords):
//   view_proj  [16]   row-major 4×4
//   params0    [4]    light_dir.xyz, debug_mode
//   params1    [4]    ambient, diffuse_strength, height_min, height_max

cbuffer Constants : register(b0)
{
    float4x4 view_proj;
    float4   params0;
    float4   params1;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float3 color    : COLOR;
    float  weight   : TEXCOORD0;
};

struct VSOutput
{
    float4 sv_position : SV_POSITION;
    float3 normal      : NORMAL;
    float3 color       : COLOR0;
    float  weight      : TEXCOORD0;
    float3 world_pos   : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.sv_position = mul(view_proj, float4(input.position, 1.0f));
    output.normal      = input.normal;
    output.color       = input.color;
    output.weight      = input.weight;
    output.world_pos   = input.position;
    return output;
}
