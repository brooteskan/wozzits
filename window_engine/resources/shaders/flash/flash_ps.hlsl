// resources/shaders/flash/flash_ps.hlsl
//
// Unlit emissive "flash" pixel shader. Outputs a fixed bright colour whose ALPHA
// is the driven 'intensity' constant, so under alpha blending the cube fades from
// bright to fully transparent as the cannon_flash behavior ramps intensity 1->0.
// The program uses DepthMode::TestNoWrite, so a flash is occluded by terrain in
// front of it but does not itself punch the depth buffer.

cbuffer FlashConstants : register(b0, space2)
{
    float4x4 mvp;       // c0..c3 (declared so 'intensity' lands at c4.x)
    float    intensity; // c4.x   driven 1->0 by cannon_flash
};

struct PSInput { float4 pos : SV_POSITION; };

float4 main(PSInput input) : SV_TARGET
{
    // Bright warm muzzle-flash colour. Tune here, or promote to an authored
    // 'color' tail constant + per-node override to make muzzle vs impact differ.
    float3 base = float3(1.0f, 0.80f, 0.45f);
    return float4(base, saturate(intensity));
}
