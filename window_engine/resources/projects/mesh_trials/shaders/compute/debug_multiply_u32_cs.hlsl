StructuredBuffer<uint> Input : register(t0);
RWStructuredBuffer<uint> Output : register(u0);

cbuffer Constants : register(b0)
{
    uint Factor;
    uint Count;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x < Count) {
        Output[id.x] = Input[id.x] * Factor;
    }
}
