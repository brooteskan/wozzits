// Composite-material fixture PS BODY (issues #285/#288). Sibling of
// custom_field_ps.hlsl: same shape, but it samples the MATERIAL_ALBEDO row
// instead of a scalar field, because the binding prelude names each declaration
// after its layout semantic -- so a shader is written against the layout it is
// compiled with, not against a register number.
//
// Declarations (material_albedo, sampler0, the "tint" tail constant) come from
// the prelude node generated off layout node 21.

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float4 albedo = material_albedo.SampleLevel(sampler0, input.uv, 0.0f);
    return float4(albedo.rgb * tint.rgb, tint.a);
}
