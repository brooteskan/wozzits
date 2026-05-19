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

    // Match gaussian_radius from the vertex shader.
    // Outside 3 sigma, contribution is tiny, so discard.
    if (r2 > 9.0f)
    {
        discard;
    }

    // Gaussian density falloff:
    //
    // r = 0 -> 1.0
    // r = 1 -> 0.607
    // r = 2 -> 0.135
    // r = 3 -> 0.011
    float gaussian = exp(-0.5f * r2);

    // Temporary readability scale.
    // This keeps heavy overlap from turning the cloud into white clay.
    float alpha_scale = 0.25f;

    float alpha = saturate(input.opacity * gaussian * alpha_scale);

    return float4(input.color, alpha);
}