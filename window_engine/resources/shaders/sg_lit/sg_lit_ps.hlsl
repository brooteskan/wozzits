// resources/shaders/sg_lit/sg_lit_ps.hlsl
//
// SG-lit PBR surface pixel shader (lighting model Seam 3, umbrella #259). A
// direct HLSL port of wz::engine::lighting (sg_lighting.cpp), evaluated against
// the resident sky_gaussian lobe buffer: diffuse (cosine lobe as an SG) +
// warped-NDF specular ("sky in reflections"). Surface params are hardcoded for
// first light; authoring them as tail constants is a follow-up.
//
// The lobe struct matches ResidentSkyLobe (32 bytes) from B1c.

cbuffer LitConstants : register(b0, space2)
{
    float4x4 world;               // c0..c3 (unused here; keeps the head aligned)
    float4x4 view_proj;           // c4..c7 (unused here)
    float4   camera_and_diameter; // c8   (xyz = camera world pos)
};

struct SkyLobe
{
    float3 direction;   // unit axis
    float  sharpness;   // lambda
    float3 amplitude;   // RGB radiance
    float  pad;         // -> 32 bytes
};

struct SkyPoint
{
    float3 direction;   // unit axis toward the emitter
    float  solid_angle; // steradians
    float3 radiance;    // RGB
    float  pad;         // -> 32 bytes, matches ResidentSkyPoint
};

StructuredBuffer<SkyLobe>  sky_gaussian        : register(t3, space2);
StructuredBuffer<SkyPoint> sky_gaussian_points : register(t4, space2);

static const float PI = 3.14159265358979323846f;

// Closed-form product integral of two SGs, per channel (sg_lighting.cpp).
float3 sg_inner_product(
    float3 axis_a, float sharp_a, float3 amp_a,
    float3 axis_b, float sharp_b, float3 amp_b)
{
    float3 dm = axis_a * sharp_a + axis_b * sharp_b;
    float  lm = max(length(dm), 1e-8f);
    float  scale =
        exp(lm - sharp_a - sharp_b) * (2.0f * PI / lm) * (1.0f - exp(-2.0f * lm));
    return amp_a * amp_b * scale;
}

float3 fresnel_schlick(float3 f0, float cos_theta)
{
    float f = pow(1.0f - saturate(cos_theta), 5.0f);
    return f0 + (1.0f - f0) * f;
}

float smith_ggx_v1(float m2, float n_dot_x)
{
    float x = max(n_dot_x, 1e-4f);
    return 1.0f / (x + sqrt(m2 + (1.0f - m2) * x * x));
}

// Scalar GGX NDF + Smith G1 (height form) for the EXACT punctual BRDF used by
// the emitter (sun) layer -- delta lights need no SG convolution.
float ggx_ndf(float n_dot_h, float m2)
{
    float d = n_dot_h * n_dot_h * (m2 - 1.0f) + 1.0f;
    return m2 / (PI * d * d + 1e-9f);
}

float smith_g1(float n_dot_x, float m2)
{
    float x = max(n_dot_x, 0.0f);
    return 2.0f * x / (x + sqrt(m2 + (1.0f - m2) * x * x) + 1e-9f);
}

struct PSInput
{
    float4 pos       : SV_POSITION;
    float3 world_pos : WORLDPOS;
    float3 world_nrm : NORMAL;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.world_nrm);
    float3 v = normalize(camera_and_diameter.xyz - input.world_pos);
    // The program rasterizes with CullNone, so the back hemisphere also draws and
    // z-fights the front at the silhouette (the speckled rim). Real outward
    // normals let us drop back-facing fragments here, winding-independently, so
    // only the front hemisphere shades -> a clean edge.
    if (dot(n, v) <= 0.0f) {
        discard;
    }

    // Hardcoded surface (first light).
    const float3 albedo    = float3(0.62f, 0.62f, 0.65f);
    const float  roughness = 0.35f;
    const float  metalness = 0.0f;

    const float3 f0          = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);
    const float3 diff_albedo = albedo * (1.0f - metalness);

    // Diffuse: clamped cosine as an energy-normalized SG (integral = pi).
    const float lc = 2.133f;
    const float ac = lc / (2.0f * (1.0f - exp(-2.0f * lc)));

    // Specular: GGX NDF as an SG warped about the reflection vector.
    const float  n_dot_v    = max(dot(n, v), 1e-4f);
    const float  m2         = max(roughness * roughness, 1e-4f);
    const float3 r          = normalize(n * (2.0f * dot(n, v)) - v);
    // Clamp n.v in the warp denominator: at grazing it would explode the lobe
    // sharpness into a near-delta that aliases into bright rim pixels.
    const float  warp_sharp = (2.0f / m2) / (4.0f * max(n_dot_v, 0.2f));
    const float  ndf_amp    = 1.0f / (PI * m2);
    const float  n_dot_r    = max(dot(n, r), 0.0f);
    const float  vis        = smith_ggx_v1(m2, n_dot_r) * smith_ggx_v1(m2, n_dot_v);
    const float3 fresnel    = fresnel_schlick(f0, n_dot_v);
    const float  spec_weight = vis * n_dot_r;

    uint count, stride;
    sky_gaussian.GetDimensions(count, stride);

    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    float3 specular   = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0u; i < count; ++i)
    {
        SkyLobe g = sky_gaussian[i];

        irradiance += sg_inner_product(
            g.direction, g.sharpness, g.amplitude,
            n, lc, float3(ac, ac, ac));

        float3 sc = sg_inner_product(
            g.direction, g.sharpness, g.amplitude,
            r, warp_sharp, float3(ndf_amp, ndf_amp, ndf_amp));
        specular += max(sc * fresnel * spec_weight, 0.0f);
    }

    float3 color = diff_albedo * irradiance / PI + specular;

    // Source B: distant emitters (the sun). Delta lights -> exact Cook-Torrance,
    // a port of emitter_radiance() from sg_lighting.cpp.
    uint pcount, pstride;
    sky_gaussian_points.GetDimensions(pcount, pstride);
    for (uint j = 0u; j < pcount; ++j)
    {
        SkyPoint p = sky_gaussian_points[j];
        const float3 l = p.direction;
        const float  n_dot_l = dot(n, l);
        if (n_dot_l <= 0.0f) {
            continue;
        }

        const float3 irr = p.radiance * p.solid_angle;   // perpendicular irradiance

        const float3 diffuse = diff_albedo * irr * (n_dot_l / PI);

        const float3 hh = normalize(l + v);
        const float  n_dot_h = max(dot(n, hh), 0.0f);
        const float  v_dot_h = max(dot(v, hh), 0.0f);
        const float  Dp = ggx_ndf(n_dot_h, m2);
        const float  Gp = smith_g1(n_dot_l, m2) * smith_g1(n_dot_v, m2);
        const float3 Fp = fresnel_schlick(f0, v_dot_h);
        const float3 spec = Fp * irr * (Dp * Gp / (4.0f * n_dot_v));

        color += diffuse + spec;
    }

    return float4(color, 1.0f);
}
