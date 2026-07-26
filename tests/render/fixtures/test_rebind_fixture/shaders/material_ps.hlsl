// Composite-material fixture PS BODY (issues #285/#288). Sibling of
// custom_field_ps.hlsl: same shape, but it samples the MATERIAL_ALBEDO row
// instead of a scalar field, because the binding prelude names each declaration
// after its layout semantic -- so a shader is written against the layout it is
// compiled with, not against a register number.
//
// Declarations (material_albedo, sampler0, the "tint" tail constant) come from
// the prelude node generated off layout node 21; MaterialVertex comes from
// material_shared.hlsl on the shared_source port (#289), so the VS and PS agree
// on the interpolants by construction.

float4 main(MaterialVertex input) : SV_TARGET
{
    float4 albedo = material_albedo.SampleLevel(sampler0, input.uv, 0.0f);
    return float4(albedo.rgb * tint.rgb, tint.a);
}
