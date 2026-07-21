// resources/shaders/overlay/overlay_vs.hlsl
//
// A 2D overlay's screen-space vertex shader. It takes the procedural Quad mesh
// (corners in [-1,1] on the XY plane, pulled as positions/indices) and places it
// at an authored PIXEL rect, mapping to NDC with the Screen view head's viewport
// size -- so the overlay keeps the same pixel size at any resolution.
//
// The object block carries the per-draw rect (+ alpha for the PS); the Screen
// view head (seam 2) carries the viewport at t0 of register space 0, filled once
// per frame by the renderer. The renderer records overlays LAST
// (DrawLayer::Overlay, seam 3), depth-disabled and alpha-blended, so this draws
// over the scene.
//
// The layout must declare view_head = Screen; this shader hand-declares the
// matching row (same register + WzScreenConstants layout the Screen prelude
// emits), which the renderer binds by the ScreenConstants semantic.
struct WzScreenConstants
{
    float4 viewport;   // xy = width, height; zw = 1/width, 1/height
};
StructuredBuffer<WzScreenConstants> screen_constants : register(t0, space0);

cbuffer OverlayBlock : register(b0, space2)
{
    float4 rect;    // xy = top-left corner in pixels, zw = size in pixels
    float4 params;  // x = alpha (used by the pixel shader), yzw unused
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    uint   idx = indices[vid];
    float3 p   = positions[idx];   // quad corner in [-1, 1], XY plane

    // Corner in [0,1] with (0,0) at the sprite's TOP-LEFT (mesh +Y is up, but a
    // sprite's rows run top-down, so flip Y here).
    float2 t = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);

    // Position within the pixel rect, then pixels -> NDC. Pixels are y-down and
    // NDC is y-up, so flip Y at the end.
    float2 px  = rect.xy + t * rect.zw;
    float2 vp  = screen_constants[0].viewport.xy;
    float2 ndc = px * (2.0f / vp) - 1.0f;

    VSOut o;
    o.pos = float4(ndc.x, -ndc.y, 0.0f, 1.0f);
    o.uv  = t;
    return o;
}
