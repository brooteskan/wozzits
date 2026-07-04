// Custom-renderable fixture PS BODY (issues #229/#231): declarations come
// from the binding-prelude node (graph node 19) generated off the authored
// layout (node 15) — scalar_field_texture at its row-derived register,
// sampler0, and the "tint" tail constant at its packed cbuffer offset. The
// texture declares as Texture2D<float4>, so the single-channel field value is
// read from .r.

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float h = scalar_field_texture.SampleLevel(sampler0, input.uv, 0.0f).r;
    return float4(tint.rgb * (0.25f + 0.75f * h), tint.a);
}
