// resources/shaders/sg_lit/sg_lighting_common.hlsl
//
// The SG-lit BRDF, shared by every sg_lit pixel shader (issue #289). A direct
// HLSL port of wz::engine::lighting (sg_lighting.cpp), evaluated against the
// resident sky_gaussian lobe buffer: diffuse (cosine lobe as an SG) +
// warped-NDF specular ("sky in reflections"), then the exact Cook-Torrance
// layer for the distant emitters (sun/moon).
//
// This is a SOURCE DEPENDENCY, not an #include: dx12_shader.cpp concatenates a
// shader's declared source deps in order, so a program wires
// [sg_lighting_common, <entry>] and the entry body just calls sg_shade. That
// keeps the sharing inside the asset graph -- the dependency is declared, so it
// folds into the shader's content hash and editing this file re-keys and
// recompiles every shader that uses it. A real #include would be discovered
// rather than declared, and unless the discovery fed back into the key it would
// reintroduce exactly the silent-stale-shader failure #283 removed.
//
// Everything a variant does NOT share stays in the variant: its own PSInput,
// its own material bindings (a texture + sampler), and its own main.

cbuffer LitConstants : register(b0, space2)
{
    float4x4 world;               // c0..c3 (unused in the PS; keeps the head aligned)
    float4x4 view_proj;           // c4..c7 (unused in the PS)
    float4   camera_and_diameter; // c8   (xyz = camera world pos)
};

// Matches ResidentSkyLobe (32 bytes) from B1c.
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

// 1 - exp(-x) for x >= 0, without the cancellation.
//
// Written naively, exp(-x) rounds to exactly 1.0f once x falls below the float32
// epsilon at 1.0 (1.19e-7), so `1 - exp(-x)` becomes exactly 0 and annihilates
// whatever it multiplies. That is what happened at the SG sharpness floor below:
// the term the floor exists to keep finite was instead driven to zero, so a very
// broad lobe -- the most uniform sky there is -- contributed no light at all.
// The CPU twin uses -expm1(-x); HLSL has no expm1, so use the series, whose
// truncation error at the switch point is ~x^3/6 = 1.7e-10 relative.
// (issue #316, C3-C1)
float minus_expm1(float x)
{
    return (x < 1e-3f) ? (x - 0.5f * x * x) : (1.0f - exp(-x));
}

// Closed-form product integral of two SGs, per channel (sg_lighting.cpp).
float3 sg_inner_product(
    float3 axis_a, float sharp_a, float3 amp_a,
    float3 axis_b, float sharp_b, float3 amp_b)
{
    float3 dm = axis_a * sharp_a + axis_b * sharp_b;
    float  lm = max(length(dm), 1e-8f);
    float  scale =
        exp(lm - sharp_a - sharp_b) * (2.0f * PI / lm) * minus_expm1(2.0f * lm);
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

// The whole shade: sky dome (SG) + distant emitters (exact), for a surface with
// the given albedo. `n` and `v` are unit world-space normal and view vectors.
// The variants differ only in where albedo comes from -- a constant, or a
// material texture -- so that is the only thing passed in.
float3 sg_shade(
    float3 n, float3 v, float3 albedo, float roughness, float metalness)
{
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

    return color;
}
