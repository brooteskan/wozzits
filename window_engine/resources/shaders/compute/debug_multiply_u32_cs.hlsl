StructuredBuffer<uint> Input : register(t0);
RWStructuredBuffer<uint> Output : register(u0);

cbuffer Params : register(b0)
{
    uint Factor;
    uint Count;
    uint2 Pad;
};

[numthreads(4, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x < Count) {
        Output[id.x] = Input[id.x] * Factor;
    }
}
