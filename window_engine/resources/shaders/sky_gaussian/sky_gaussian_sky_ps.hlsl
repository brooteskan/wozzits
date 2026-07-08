// resources/shaders/sky_gaussian/sky_gaussian_sky_ps.hlsl
//
// Custom-renderable pixel shader for the spherical-Gaussian sky (Seam B1-iii,
// issue #261). Sums the resident lobe StructuredBuffer (variant "sky_gaussian",
// 32-byte ResidentSkyLobe from B1c) in the origin-relative view direction the VS
// handed down. This is evaluate_set() from sky_gaussian.cpp ported to HLSL:
//
//     L(d) = sum_i  amplitude_i * exp( sharpness_i * (dot(direction_i, d) - 1) )
//
// The lobe count comes from the buffer itself (GetDimensions), so nothing has to
// be authored to match the baked asset.
//
// NOTE (v1): the sun lives in the fit's point_sources, which B1c left out of the
// resident buffer (lobes-only). So this renders the sky DOME + glow but not the
// crisp sun -- expected, not a bug. Point-source rendering is a follow-up.

struct SkyLobe
{
    float3 direction;   // unit axis
    float  sharpness;   // lambda
    float3 amplitude;   // RGB radiance
    float  pad;         // -> 32 bytes, matches ResidentSkyLobe
};

StructuredBuffer<SkyLobe> sky_gaussian : register(t2, space2);

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 dir : DIRECTION;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 d = normalize(input.dir);

    uint count, stride;
    sky_gaussian.GetDimensions(count, stride);

    float3 c = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0u; i < count; ++i)
    {
        SkyLobe g = sky_gaussian[i];
        c += g.amplitude * exp(g.sharpness * (dot(g.direction, d) - 1.0f));
    }

    return float4(c, 1.0f);
}
