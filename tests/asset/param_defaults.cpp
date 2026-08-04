#include <gtest/gtest.h>

#include <support/fp_expectations.h>

#include <asset/compiler.h>
#include <asset/param_defaults.h>

#include <array>
#include <cfenv>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

namespace
{
    using namespace wz::asset;

    ParamDecl decl(
        std::string_view name,
        ParamType type,
        double default_num = 0.0,
        std::string_view default_str = {})
    {
        ParamDecl d{};
        d.name = name;
        d.type = type;
        d.default_num = default_num;
        d.default_str = default_str;
        return d;
    }
}

TEST(ParamDefaults, DefaultValueCoversEveryParamType)
{
    EXPECT_TRUE(std::get<bool>(default_param_value(decl("b", ParamType::Bool, 1.0))));
    EXPECT_FALSE(std::get<bool>(default_param_value(decl("b0", ParamType::Bool, 0.0))));
    EXPECT_EQ(std::get<int64_t>(default_param_value(decl("i", ParamType::Int, 7.0))), 7);
    EXPECT_EQ(std::get<int64_t>(default_param_value(decl("e", ParamType::Enum, 2.0))), 2);
    EXPECT_DOUBLE_EQ(
        std::get<double>(default_param_value(decl("f", ParamType::Float, 1.5))),
        1.5);

    const auto v3 =
        std::get<std::array<float, 3>>(default_param_value(decl("v", ParamType::Float3, 2.0)));
    EXPECT_EQ(v3, (std::array<float, 3>{ 2.f, 2.f, 2.f }));

    const auto col =
        std::get<std::array<float, 3>>(default_param_value(decl("c", ParamType::Color, 0.5)));
    EXPECT_EQ(col, (std::array<float, 3>{ 0.5f, 0.5f, 0.5f }));

    EXPECT_EQ(
        std::get<std::string>(default_param_value(decl("s", ParamType::String, 0.0, "hi"))),
        "hi");
    EXPECT_EQ(
        std::get<std::string>(default_param_value(decl("p", ParamType::FilePath, 0.0, "a/b.txt"))),
        "a/b.txt");
}

TEST(ParamDefaults, ValueMatchesDecl)
{
    EXPECT_TRUE(param_value_matches_decl(ParamValue{ true }, decl("b", ParamType::Bool)));
    EXPECT_FALSE(param_value_matches_decl(ParamValue{ int64_t{ 1 } }, decl("b", ParamType::Bool)));
    EXPECT_TRUE(param_value_matches_decl(ParamValue{ int64_t{ 3 } }, decl("i", ParamType::Int)));
    EXPECT_TRUE(param_value_matches_decl(ParamValue{ int64_t{ 3 } }, decl("e", ParamType::Enum)));
    EXPECT_TRUE(param_value_matches_decl(ParamValue{ 2.0 }, decl("f", ParamType::Float)));
    EXPECT_TRUE(param_value_matches_decl(
        ParamValue{ std::array<float, 3>{} },
        decl("v", ParamType::Float3)));
    EXPECT_TRUE(param_value_matches_decl(
        ParamValue{ std::string{ "x" } },
        decl("s", ParamType::String)));
    EXPECT_FALSE(param_value_matches_decl(ParamValue{ 2.0 }, decl("s", ParamType::String)));
}

TEST(ParamDefaults, EnsureBlockFillsRepairsAndPreserves)
{
    AssetCompiler compiler{};
    compiler.parameters = {
        decl("present_ok", ParamType::Int, 5.0),
        decl("present_wrong_type", ParamType::Float, 9.0),
        decl("missing", ParamType::String, 0.0, "def"),
    };

    ParamBlock block;
    block.values["present_ok"] = int64_t{ 42 };                  // correct type, user value
    block.values["present_wrong_type"] = std::string{ "oops" };  // wrong type for a Float
    block.values["extra"] = std::string{ "keep" };               // not in schema

    ensure_param_block_defaults(block, compiler);

    EXPECT_EQ(std::get<int64_t>(block.values.at("present_ok")), 42);                  // preserved
    EXPECT_DOUBLE_EQ(std::get<double>(block.values.at("present_wrong_type")), 9.0);   // repaired
    EXPECT_EQ(std::get<std::string>(block.values.at("missing")), "def");             // filled
    EXPECT_EQ(std::get<std::string>(block.values.at("extra")), "keep");              // untouched
}

// C1-C64 (#314): ParamBlock::get<float> is the last gate before an authored
// param leaves the block. A hostile double (1e308 -> +inf as a float, or a NaN)
// stored in the variant must not reach a consumer that divides by it or feeds it
// to the GPU; get<> returns the caller's own declared default instead.
//
// LOAD-BEARING: revert-checked -- without the isfinite gate in get<>'s float
// branch the overflow/nan EXPECTs see +inf / NaN and fail.
TEST(ParamBlockGet, NonFiniteAuthoredFloatFallsBackToDefault)
{
    ParamBlock pb;
    pb.values["nan"] = std::numeric_limits<double>::quiet_NaN();
    pb.values["ok"] = 2.5;

    EXPECT_FLOAT_EQ(pb.get<float>("nan", 60.0f), 60.0f);   // NaN -> default
    EXPECT_FLOAT_EQ(pb.get<float>("ok", 60.0f), 2.5f);     // real value passes
    EXPECT_FLOAT_EQ(pb.get<float>("missing", 7.0f), 7.0f); // absent -> default

    // Narrowing 1e308 to a float overflows to +inf and raises FE_OVERFLOW at the
    // cast (the same as narrow_float does; harmless in production where flags are
    // unchecked). Declare it so the FP-exception listener stays quiet AND so the
    // test asserts the overflow really happens.
    pb.values["overflow"] = 1e308;
    {
        wz::testing::ExpectFpException overflow_expected{ FE_OVERFLOW };
        EXPECT_FLOAT_EQ(pb.get<float>("overflow", 60.0f), 60.0f);  // fell back
    }
}
