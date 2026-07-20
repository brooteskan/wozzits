// resources/shaders/haze/haze_ps.hlsl
//
// Pixel shader for the HAZE DOME, now ENVIRONMENT-LIT. Instead of a single
// authored fog colour, the haze takes its COLOUR + DIRECTIONALITY from a
// spherical-Gaussian sky -- the same resident lobe/point buffers the sg_lit
// surfaces and the sky dome read. Physically this is airlight: haze is
// in-scattered environment light, so it glows toward bright sky (the sun) and
// dims toward the dark side -- which reads as depth ("something over there,
// beyond the horizon").
//
// The sky it scatters is a per-renderable CHOICE (the sky_gaussian binding on the
// renderable), so the haze can be lit by a DIFFERENT sky than the one rendered as
// the visible backdrop -- lighting from one dome, appearance from another.
//
// Division of labour: the ATMOSPHERE (view-frequency CameraFog, t0/space0) still
// authors the AMOUNT + PROFILE of the haze -- density, the horizon band, on/off --
// while the sky provides the COLOUR. fog_color becomes a spectral TINT on the
// scattered sky radiance (set it white for pure sky colour).
//
// Object-space bindings (register space 2) mirror the sky dome
// (sky_gaussian_sky_ps.hlsl) so the SG eval is byte-for-byte identical:
//   t2  sky_gaussian         -- low-frequency dome lobes (the diffuse sky)
//   t3  sky_gaussian_points  -- high-frequency emitters (sun / moon)
// The Haze Sphere Binding Layout must declare these two SRV rows (binding2 /
// binding3) or the shader's t2/t3 go unbound; see sky_layout (node 78) for the
// exact rows.

struct WzViewConstants
{
    float4 camera_world_position;
    float4 fog_color_density;   // rgb = fog_color (now a tint), w = fog_density
    float4 fog_params;          // x = start_distance, y = height_falloff,
                                //   z = enabled (0/1), w = unused
};

StructuredBuffer<WzViewConstants> view_constants : register(t0, space0);

// 32-byte ResidentSkyLobe / ResidentSkyPoint -- identical to the sg_lit and
// sky-dome shaders, so this reads the same resident buffers.
struct SkyLobe
{
    float3 direction;   // unit axis
    float  sharpness;   // lambda
    float3 amplitude;   // RGB radiance
    float  pad;
};

struct SkyPoint
{
    float3 direction;   // unit axis toward the emitter
    float  solid_angle; // steradians
    float3 radiance;    // RGB
    float  pad;
};

StructuredBuffer<SkyLobe>  sky_gaussian        : register(t2, space2);
StructuredBuffer<SkyPoint> sky_gaussian_points : register(t3, space2);

static const float kPI = 3.14159265358979323846f;

// Fallback band reach when the atmosphere leaves fog_height_falloff at 0 (see the
// band computation below); the haze still shows out of the box.
static const float kHazeBandReachDefault = 0.6f;

struct PSIn
{
    float4 pos : SV_POSITION;
    float3 dir : DIRECTION;
};

// The sky radiance in direction d -- evaluate_set() from sky_gaussian.cpp, the
// SAME loop as sky_gaussian_sky_ps.hlsl: low-frequency lobes plus a soft bright
// disc per point emitter. This is the environment light the haze scatters.
float3 sky_radiance(float3 d)
{
    float3 c = float3(0.0f, 0.0f, 0.0f);

    uint count, stride;
    sky_gaussian.GetDimensions(count, stride);
    for (uint i = 0u; i < count; ++i)
    {
        SkyLobe g = sky_gaussian[i];
        c += g.amplitude * exp(g.sharpness * (dot(g.direction, d) - 1.0f));
    }

    uint pcount, pstride;
    sky_gaussian_points.GetDimensions(pcount, pstride);
    for (uint j = 0u; j < pcount; ++j)
    {
        SkyPoint p = sky_gaussian_points[j];
        const float cos_r = 1.0f - p.solid_angle / (2.0f * kPI);      // disc edge
        const float cos_c = 1.0f - (1.0f - cos_r) * 0.5f;            // solid core
        const float disc  = smoothstep(cos_r, cos_c, dot(d, p.direction));
        c += p.radiance * disc;
    }
    return c;
}

float4 main(PSIn input) : SV_TARGET
{
    WzViewConstants v = view_constants[0];

    float3 fog_tint = v.fog_color_density.rgb;
    float  density  = v.fog_color_density.w;
    float  enabled  = v.fog_params.z;

    float3 dir = normalize(input.dir);

    // COLOUR from the environment: the sky radiance in the view direction, tinted
    // by the authored fog colour (white => pure sky colour). Falls back to the
    // flat fog colour when no sky is bound (empty lobe set) so the dome still shows
    // -- e.g. a renderable that never got a sky_gaussian source wired.
    uint count, stride;
    sky_gaussian.GetDimensions(count, stride);
    float3 haze_color =
        (count > 0u) ? sky_radiance(dir) * fog_tint : fog_tint;

    // AMOUNT from the atmosphere: the horizon band (densest at the horizon,
    // thinning to nothing by `reach` elevation) times the authored density. Same
    // profile as before -- the atmosphere still owns how much haze there is and
    // where it sits, independent of what colour the sky paints it.
    float reach = (v.fog_params.y > 1e-4f) ? v.fog_params.y : kHazeBandReachDefault;
    float elevation = saturate(input.dir.y);      // sin of the angle above horizon
    float band = 1.0f - smoothstep(0.0f, reach, elevation);

    float alpha = saturate(band * density) * enabled;

    return float4(haze_color, alpha);
}
