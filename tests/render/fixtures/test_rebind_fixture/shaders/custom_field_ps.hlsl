// Custom-renderable fixture PS (issue #229): samples the scalar-field texture
// bound by SEMANTIC through the scene node's renderable_bindings (layout row
// scalar_field_texture -> t2 space2) and tints by the "tint" constant, whose
// value comes from the node's per-instance renderable_constants override.
cbuffer CustomBlock : register(b0, space2)
{
    float4x4 mvp;
    float4   tint;
};

Texture2D<float> fieldTex     : register(t2, space2);
SamplerState     fieldSampler : register(s0, space2);

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float h = fieldTex.SampleLevel(fieldSampler, input.uv, 0.0f);
    return float4(tint.rgb * (0.25f + 0.75f * h), tint.a);
}
