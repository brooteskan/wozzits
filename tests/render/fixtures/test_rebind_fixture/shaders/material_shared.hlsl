// Shared between the composite-material fixture's VS and PS (#289). The two
// stages MUST agree on this struct, and before shared sources existed the only
// way to say that was to write it twice and hope -- the same shape of problem
// as the duplicated sg_lit BRDF, from the other direction.
//
// Wired to both shader nodes on their "shared_source" port, so it is
// concatenated AHEAD of the entry body. That is a declared dependency, so it
// folds into each shader's key: editing this file re-keys and recompiles both.
struct MaterialVertex
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
