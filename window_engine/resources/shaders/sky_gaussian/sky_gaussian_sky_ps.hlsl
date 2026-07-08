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
// The high-frequency layer (sun/moon/stars) rides a SECOND resident buffer
// (variant "sky_gaussian_points", Seam 4-B-ii): each is drawn as a bright
// angular cap around its direction. Omega = 2*pi*(1-cos r), so a point is inside
// the disc when dot(viewdir, ps.dir) >= cos r.

static const float PI = 3.14159265358979323846f;

struct SkyLobe
{
    float3 direction;   // unit axis
    float  sharpness;   // lambda
    float3 amplitude;   // RGB radiance
    float  pad;         // -> 32 bytes, matches ResidentSkyLobe
};

struct SkyPoint
{
    float3 direction;   // unit axis
    float  solid_angle; // steradians
    float3 radiance;    // RGB
    float  pad;         // -> 32 bytes, matches ResidentSkyPoint
};

StructuredBuffer<SkyLobe>  sky_gaussian        : register(t2, space2);
StructuredBuffer<SkyPoint> sky_gaussian_points : register(t3, space2);

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

    // Point sources (the sun): a soft-edged bright disc per emitter.
    uint pcount, pstride;
    sky_gaussian_points.GetDimensions(pcount, pstride);
    for (uint j = 0u; j < pcount; ++j)
    {
        SkyPoint p = sky_gaussian_points[j];
        const float cos_r = 1.0f - p.solid_angle / (2.0f * PI);      // disc edge
        const float cos_c = 1.0f - (1.0f - cos_r) * 0.5f;           // solid core
        const float disc  = smoothstep(cos_r, cos_c, dot(d, p.direction));
        c += p.radiance * disc;
    }

    return float4(c, 1.0f);
}
