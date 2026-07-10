// resources/shaders/bubble/bubble_ps.hlsl
//
// Teleport-bubble pixel shader: a translucent, glowing cyan shell. It clones the
// flash program's transparent setup (BlendMode::AlphaBlend + DepthMode::
// TestNoWrite + RasterMode::SolidCullNone) but emits a FIXED cyan colour at a
// fixed alpha, so the bubble is statically visible for Seam 1 -- you can see the
// terrain / tank through it. Later the alpha (and the node scale) can be driven
// for the expand/shrink beats.
//
// It reuses the flash program's vertex shader (flash_vs) and binding layout
// (flash_layout), which pull the mesh and place 'intensity' at c4.x. This static
// bubble ignores 'intensity'; the block is declared only to keep the layout's
// constant offsets aligned with the shared VS.

cbuffer BubbleConstants : register(b0, space2)
{
    float4x4 mvp;       // c0..c3  per-draw transform (used by the shared VS)
    float    intensity; // c4.x    driven look; unused by the static bubble
};

struct PSInput { float4 pos : SV_POSITION; };

float4 main(PSInput input) : SV_TARGET
{
    // Bright cyan energy shell; the 0.40 alpha makes it read as translucent glass
    // under alpha blending. Tune the colour / alpha here.
    const float3 cyan = float3(0.15f, 0.85f, 1.0f);
    return float4(cyan, 0.40f);
}
