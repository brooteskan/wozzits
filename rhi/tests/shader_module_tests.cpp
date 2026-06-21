#include "wz_test.h"

#include <wozzits/rhi/shader_module.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

using namespace wz::rhi;

namespace
{
    bool bytes_equal(std::span<const uint8_t> a,
                     std::span<const uint8_t> b)
    {
        return a.size() == b.size()
            && std::equal(a.begin(), a.end(), b.begin());
    }
}

static void register_find_get()
{
    ShaderModuleRegistry modules;
    const std::vector<uint8_t> vs = { 1, 2, 3 };
    const std::vector<uint8_t> ps = { 4, 5, 6, 7 };

    const Tag vs_tag = modules.register_program(ShaderModuleDesc{
        "asset:vertex",
        ShaderStage::Vertex,
        vs });
    const Tag ps_tag = modules.register_program(ShaderModuleDesc{
        "asset:pixel",
        ShaderStage::Pixel,
        ps });

    WZ_CHECK(vs_tag.valid());
    WZ_CHECK(ps_tag.valid());
    WZ_CHECK_FALSE(vs_tag == ps_tag);
    WZ_CHECK(modules.find("asset:vertex") == vs_tag);
    WZ_CHECK(modules.find("asset:pixel") == ps_tag);

    const ShaderModuleDesc* vertex = modules.get(vs_tag);
    const ShaderModuleDesc* pixel = modules.get(ps_tag);
    WZ_CHECK(vertex != nullptr);
    WZ_CHECK(pixel != nullptr);
    if (vertex && pixel) {
        WZ_CHECK(bytes_equal(vertex->bytecode, vs));
        WZ_CHECK(bytes_equal(pixel->bytecode, ps));
    }
}

static void missing_module_is_null_tag()
{
    ShaderModuleRegistry modules;
    WZ_CHECK_FALSE(modules.find("asset:does_not_exist").valid());
}

static void resolve_happy_path()
{
    ShaderModuleRegistry modules;
    const std::vector<uint8_t> vs = { 0x10, 0x20 };
    const std::vector<uint8_t> ps = { 0x30, 0x40, 0x50 };
    WZ_CHECK(modules.register_program(ShaderModuleDesc{
        "asset:vertex",
        ShaderStage::Vertex,
        vs }).valid());
    WZ_CHECK(modules.register_program(ShaderModuleDesc{
        "asset:pixel",
        ShaderStage::Pixel,
        ps }).valid());

    RenderProgramDesc program;
    program.vertex_shader = "asset:vertex";
    program.pixel_shader = "asset:pixel";

    const std::optional<ProgramBytecode> bytecode =
        resolve_program_bytecode(program, modules);
    WZ_CHECK(bytecode.has_value());
    if (bytecode) {
        WZ_CHECK(bytes_equal(bytecode->vertex, vs));
        WZ_CHECK(bytes_equal(bytecode->pixel, ps));
    }
}

static void resolve_missing_returns_nullopt()
{
    ShaderModuleRegistry modules;
    WZ_CHECK(modules.register_program(ShaderModuleDesc{
        "asset:vertex",
        ShaderStage::Vertex,
        { 1, 2, 3 } }).valid());

    RenderProgramDesc program;
    program.vertex_shader = "asset:vertex";
    program.pixel_shader = "asset:pixel";

    WZ_CHECK_FALSE(resolve_program_bytecode(program, modules).has_value());
}

static void resolve_empty_bytecode_returns_nullopt()
{
    ShaderModuleRegistry modules;
    WZ_CHECK(modules.register_program(ShaderModuleDesc{
        "asset:vertex",
        ShaderStage::Vertex,
        { 1, 2, 3 } }).valid());
    WZ_CHECK(modules.register_program(ShaderModuleDesc{
        "asset:pixel",
        ShaderStage::Pixel,
        {} }).valid());

    RenderProgramDesc program;
    program.vertex_shader = "asset:vertex";
    program.pixel_shader = "asset:pixel";

    WZ_CHECK_FALSE(resolve_program_bytecode(program, modules).has_value());
}

static void resolve_compute_happy_path()
{
    ShaderModuleRegistry modules;
    const std::vector<uint8_t> cs = { 0x90, 0x91, 0x92 };
    WZ_CHECK(modules.register_program(ShaderModuleDesc{
        "asset:compute",
        ShaderStage::Compute,
        cs }).valid());

    ComputeProgramDesc program;
    program.compute_shader = "asset:compute";

    const std::optional<std::vector<uint8_t>> bytecode =
        resolve_compute_bytecode(program, modules);
    WZ_CHECK(bytecode.has_value());
    if (bytecode) {
        WZ_CHECK(bytes_equal(*bytecode, cs));
    }
}

static void resolve_compute_rejects_wrong_stage()
{
    ShaderModuleRegistry modules;
    WZ_CHECK(modules.register_program(ShaderModuleDesc{
        "asset:not_compute",
        ShaderStage::Pixel,
        { 0x01, 0x02 } }).valid());

    ComputeProgramDesc program;
    program.compute_shader = "asset:not_compute";

    WZ_CHECK_FALSE(resolve_compute_bytecode(program, modules).has_value());
}

static void resolve_compute_empty_bytecode_returns_nullopt()
{
    ShaderModuleRegistry modules;
    WZ_CHECK(modules.register_program(ShaderModuleDesc{
        "asset:compute",
        ShaderStage::Compute,
        {} }).valid());

    ComputeProgramDesc program;
    program.compute_shader = "asset:compute";

    WZ_CHECK_FALSE(resolve_compute_bytecode(program, modules).has_value());
}

int main()
{
    WZ_RUN(register_find_get);
    WZ_RUN(missing_module_is_null_tag);
    WZ_RUN(resolve_happy_path);
    WZ_RUN(resolve_missing_returns_nullopt);
    WZ_RUN(resolve_empty_bytecode_returns_nullopt);
    WZ_RUN(resolve_compute_happy_path);
    WZ_RUN(resolve_compute_rejects_wrong_stage);
    WZ_RUN(resolve_compute_empty_bytecode_returns_nullopt);
    WZ_TEST_RETURN();
}
