struct PSInput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float opacity : OPACITY;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float r2 = dot(input.uv, input.uv);

    if (r2 > 1.0f)
        discard;

    float alpha = exp(-r2 * 2.0f) * input.opacity;

    return float4(input.color, alpha);
}