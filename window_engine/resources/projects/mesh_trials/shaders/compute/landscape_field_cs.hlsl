// Behavior-driven landscape detail heatmap.
//
// Reads the mesh vertex positions the engine binds at t0
// (WZ_GPU_RESOURCE_REF_MESH_VERTEX_POSITIONS) and computes a
// multi-octave noise field in mesh-local XZ space, lifted by the
// vertex height. The output spans the full [0,1] range so the
// render shader color ramp shows distinct bands: dark terrain,
// green valleys, golden slopes, and bright ridges.

StructuredBuffer<float3> Positions : register(t0);
RWStructuredBuffer<float> Output : register(u0);

cbuffer Constants : register(b0)
{
    uint VertexCount;
    float Phase;
    float Frequency;
    float Bias;
};

// ---- pseudo-random hash (no trig) ----

float hash2d(float2 p)
{
    float3 q = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}

// ---- smoothed value noise ----

float value_noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash2d(i);
    float b = hash2d(i + float2(1.0, 0.0));
    float c = hash2d(i + float2(0.0, 1.0));
    float d = hash2d(i + float2(1.0, 1.0));

    return lerp(lerp(a, b, f.x),
                lerp(c, d, f.x), f.y);
}

// ---- fractal Brownian motion (5 octaves) ----

float fbm(float2 p, float drift)
{
    float value     = 0.0;
    float amplitude = 0.5;

    [unroll]
    for (int octave = 0; octave < 5; ++octave)
    {
        value     += amplitude * value_noise(p + drift);
        p         *= 2.17;
        drift     *= 1.31;
        amplitude *= 0.48;
    }
    return value;
}

// ---- ridge noise for sharp detail features ----

float ridge_noise(float2 p, float drift)
{
    float v = value_noise(p + drift);
    return 1.0 - abs(2.0 * v - 1.0);        // sharp V-shaped peaks
}

float ridge_fbm(float2 p, float drift)
{
    float value     = 0.0;
    float amplitude = 0.55;

    [unroll]
    for (int octave = 0; octave < 4; ++octave)
    {
        value     += amplitude * ridge_noise(p, drift);
        p         *= 2.03;
        drift     *= 1.27;
        amplitude *= 0.45;
    }
    return value;
}

// ----

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= VertexCount) { return; }

    const float3 position = Positions[id.x];

    // Sample noise in mesh-local XZ so the pattern follows the actual
    // surface instead of vertex ordering. Frequency tunes the feature
    // size relative to the mesh's local units.
    const float2 uv = position.xz * (Frequency * 0.25);
    const float drift = Phase * 0.12;

    // Two noise layers at different character:
    //   base  — smooth rolling hills          (fbm)
    //   ridge — sharp crests / drainage lines (ridge_fbm)
    const float base  = fbm(uv, drift);
    const float ridge = ridge_fbm(uv * 1.7, drift * 0.8);

    // Blend: mostly base, ridge lifts the peaks.
    float detail = lerp(base, ridge, 0.45);

    // Elevation contribution so height reads through the color ramp.
    const float height = saturate(position.y * Frequency * 0.05);
    detail = lerp(detail, height, 0.35);

    // Remap to use the full color ramp. Bias shifts the midpoint.
    detail = smoothstep(0.15, 0.85, detail + (Bias - 0.5) * 0.4);

    Output[id.x] = saturate(detail);
}
