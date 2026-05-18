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

    // Hard reject outside the ellipse quad's unit circle.
    if (r2 > 1.0f)
    {
        discard;
    }

    // Debug Gaussian footprint.
    //
    // r2 = 0   -> alpha = opacity
    // r2 = 1   -> alpha ≈ 0.135 * opacity
    //
    // This is intentionally simple for this checkpoint. Later, when the vertex
    // shader outputs a true screen-space covariance/conic, this becomes the
    // place where we evaluate exp(-0.5 * d^T C^-1 d).
    float gaussian = exp(-2.0f * r2);

    // Fade the very outer edge to avoid a visible circular cutoff.
    // This is not physically correct 3DGS yet; it is a visual debug stabilizer.
    float edge_fade = 1.0f - smoothstep(0.85f, 1.0f, r2);

    float alpha = saturate(input.opacity * gaussian * edge_fade);

    // Premultiplied output is usually the better target for splats, but this
    // only works correctly if the PSO blend state is configured for premult alpha:
    //
    //   SrcBlend      = ONE
    //   DestBlend     = INV_SRC_ALPHA
    //   BlendOp       = ADD
    //   SrcBlendAlpha = ONE
    //   DestBlendAlpha= INV_SRC_ALPHA
    //
    // If your PSO is still standard alpha blending, return float4(input.color, alpha)
    // instead.
    return float4(input.color * alpha, alpha);
}