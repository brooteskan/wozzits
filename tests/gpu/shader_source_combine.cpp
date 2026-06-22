// tests/gpu/shader_source_combine.cpp
//
// Unit coverage for wz::gpu::combine_hlsl_sources — the single definition of how
// multiple HLSL source deps concatenate into one translation unit. Both compile
// paths share it: the legacy gpu handle (compile_hlsl) and the rhi ShaderModule
// (the shader asset compiler, #193). The dual-output invariant is that both see
// byte-identical source, so the multi-source case below is the regression guard
// for #193's parity fix (the rhi path previously compiled only the primary dep).

#include <gtest/gtest.h>

#include <gpu/shader_types.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace
{
    std::span<const uint8_t> bytes_of(const std::string& s)
    {
        return std::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(s.data()), s.size() };
    }
}

TEST(CombineHlslSources, SingleSourceGetsTrailingNewline)
{
    const std::string a = "float4 main() : SV_Target { return 0; }";
    const std::vector<std::span<const uint8_t>> sources{ bytes_of(a) };

    EXPECT_EQ(wz::gpu::combine_hlsl_sources(sources), a + "\n");
}

TEST(CombineHlslSources, MultipleSourcesConcatenateInDeclaredOrder)
{
    // The crux of #193's parity fix: every dep is compiled, in declared order —
    // a header dep before a body dep — not just the primary one. This is the
    // case that diverged when the rhi path compiled sources[primary] alone.
    const std::string header = "#define K 3";
    const std::string body = "float4 main() : SV_Target { return K; }";
    const std::vector<std::span<const uint8_t>> sources{
        bytes_of(header), bytes_of(body) };

    EXPECT_EQ(wz::gpu::combine_hlsl_sources(sources),
              header + "\n" + body + "\n");
}

TEST(CombineHlslSources, EmptySourcesAreSkipped)
{
    const std::string a = "a";
    const std::string empty;
    const std::string b = "b";
    const std::vector<std::span<const uint8_t>> sources{
        bytes_of(a), bytes_of(empty), bytes_of(b) };

    // The empty middle dep contributes nothing — no stray blank line beyond the
    // per-source newline — matching the legacy concatenation exactly.
    EXPECT_EQ(wz::gpu::combine_hlsl_sources(sources), "a\nb\n");
}

TEST(CombineHlslSources, NoSourcesYieldEmpty)
{
    const std::vector<std::span<const uint8_t>> sources{};
    EXPECT_TRUE(wz::gpu::combine_hlsl_sources(sources).empty());
}
