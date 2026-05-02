#include <cstddef>
#include <cstdint>

namespace wz::gpu::dx12::internal
{
    // These MUST be valid compiled shader blobs.
    // For now: use tiny precompiled shaders (you can replace later)

    const unsigned char g_VS[] =
    {
        // compiled VS bytecode goes here
    };

    const size_t g_VS_size = sizeof(g_VS);

    const unsigned char g_PS[] =
    {
        // compiled PS bytecode goes here
    };

    const size_t g_PS_size = sizeof(g_PS);
}