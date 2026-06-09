#include <gtest/gtest.h>

#include <engine/assets/compute_pipeline/hlsl_binding_extract.h>

TEST(HlslBindingExtract, ExtractsStructuredBuffersAndRootConstants)
{
    using namespace wz::engine::assets;

    const char* source = R"(
struct Sample
{
    float3 Position;
    uint Id;
};

StructuredBuffer<Sample> Input : register(t0);
RWStructuredBuffer<uint> Output : register(u1, space2);

cbuffer Params : register(b0)
{
    uint Factor;
    float Gamma;
    uint2 Pad;
};
)";

    const HlslBindingExtraction extraction =
        extract_hlsl_bindings_from_source(source);

    ASSERT_TRUE(extraction.ok());
    EXPECT_EQ(extraction.root_constant_dwords, 4u);
    ASSERT_EQ(extraction.ports.size(), 4u);

    EXPECT_EQ(extraction.ports[0].name, "input");
    EXPECT_EQ(extraction.ports[0].port_kind, WZ_GPU_PORT_STRUCTURED_BUFFER);
    EXPECT_EQ(extraction.ports[0].direction, WZ_GPU_PORT_INPUT);
    EXPECT_EQ(
        extraction.ports[0].binding_kind,
        ComputeBindingKind::StructuredBufferSRV);
    EXPECT_EQ(extraction.ports[0].shader_register, 0u);
    EXPECT_EQ(extraction.ports[0].stride_bytes, 16u);

    EXPECT_EQ(extraction.ports[1].name, "output");
    EXPECT_EQ(extraction.ports[1].direction, WZ_GPU_PORT_OUTPUT);
    EXPECT_EQ(
        extraction.ports[1].binding_kind,
        ComputeBindingKind::StructuredBufferUAV);
    EXPECT_EQ(extraction.ports[1].shader_register, 1u);
    EXPECT_EQ(extraction.ports[1].register_space, 2u);
    EXPECT_EQ(extraction.ports[1].stride_bytes, 4u);

    EXPECT_EQ(extraction.ports[2].name, "factor");
    EXPECT_EQ(extraction.ports[2].port_kind, WZ_GPU_PORT_U32);
    EXPECT_EQ(extraction.ports[2].root_constant_offset, 0u);
    EXPECT_EQ(extraction.ports[2].root_constant_dwords, 1u);

    EXPECT_EQ(extraction.ports[3].name, "gamma");
    EXPECT_EQ(extraction.ports[3].port_kind, WZ_GPU_PORT_F32);
    EXPECT_EQ(extraction.ports[3].root_constant_offset, 1u);
    EXPECT_EQ(extraction.ports[3].root_constant_dwords, 1u);
}
