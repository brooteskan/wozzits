// resources/shaders/gaussian_splat/gaussian_splat_sphere_ps.hlsl
//
// Pixel shader for the scalar-field point cloud rendered as camera-facing
// spheres (issue #208, fix-2). Masks the billboard quad to a disc and shades it
// as a sphere impostor (a hemisphere normal derived from the disc) with a fixed
// key light, so each sample reads as a small lit sphere rather than a flat dot.
//
// Opaque: the field_splat_program is BlendMode::Opaque + DepthMode::TestWrite,
// so spheres occlude each other cleanly with no alpha "fog". The vertex-only
// root constants are not visible here -- this PS reads only the interpolants.

struct PSInput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float2 uv       : TEXCOORD0;  // [-1,1] across the quad
};

float4 main(PSInput input) : SV_TARGET
{
    float2 p  = input.uv;
    float  r2 = dot(p, p);
    if (r2 > 1.0f) {
        discard;  // outside the unit disc -> not part of the sphere
    }

    // Sphere-impostor normal: the visible hemisphere facing the camera, in the
    // quad's (right, up, toward-camera) frame.
    float  z = sqrt(saturate(1.0f - r2));
    float3 n = float3(p.x, p.y, z);

    // Fixed key light in quad space gives every sphere the same relative shading
    // (a cheap, consistent 3D read -- intentionally minimal, no scene lighting).
    float3 light = normalize(float3(0.35f, 0.45f, 0.82f));
    float  ndl   = saturate(dot(n, light));
    float  shade = 0.45f + 0.55f * ndl;

    // SOLID color (not the per-sample height-encoded color from the converter --
    // the cloud is a uniform reference, so height is NOT painted into it). The
    // impostor shading still gives a 3D read. input.color is intentionally
    // ignored; a future tweak could expose this as a renderable param.
    const float3 sphere_color = float3(0.74f, 0.78f, 0.86f);
    return float4(sphere_color * shade, 1.0f);
}
