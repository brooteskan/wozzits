#include <iostream>
#include <cstdio>
#include <expected>
#include <utility>
#include <window/window2.h>
#include <logging/logger.h>
#include <file/filesystem.h>
#include <event/platform_event.h>
#include <gpu/gpu.h>
#include <input/input.h>
#include <gpu/dx12/dx12.h>
#include <crtdbg.h>
#include <asset/types.h>
#include <asset/system.h>
#include <engine/assets/schema_registry.h>
#include <engine/assets/shader/shader_types.h>
#include <asset/compiler.h>
#include <gpu/shader.h>

#include <crtdbg.h>
#define _CRTDBG_MAP_ALLOC
// ─── Schemas ──────────────────────────────────────────────────────────────────
// These would normally be hashes of the schema name strings, produced once at
// startup. Hard-coded here for the test main.

//static constexpr wz::asset::SchemaID kHLSLFileSchema{ 0x1a2b3c4d };  // carrier
//static constexpr wz::asset::SchemaID kHLSLSchema{ 0x5e6f7a8b };  // linker
// NOTE: defined in engine/assets/schema_registry.h

using namespace wz::asset;
using namespace wz::fs;

// ─── Compile descriptor ───────────────────────────────────────────────────────
// Carried on the shader node's meta field. Tells the compiler which entry
// point, target profile, and source ordering to use.





// ─── Helpers ──────────────────────────────────────────────────────────────────

// FNV-1a 64-bit — good enough for content hashing in a test main.
static Hash fnv1a(std::span<const uint8_t> data)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : data) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return { h, ~h };   // fill both halves
}

static Hash hash_u64(uint64_t v)
{
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdull;
    v ^= v >> 33;
    v *= 0xc4ceb9fe1a85ec53ull;
    v ^= v >> 33;
    return { v, ~v };
}

static Hash hash_str(std::string_view s)
{
    return fnv1a({ reinterpret_cast<const uint8_t*>(s.data()), s.size() });
}

// Combine a sequence of keys into one hash for deps_hash.
static void mix_hash(uint64_t& lo, uint64_t& hi, Hash h)
{
    lo ^= h.lo + 0x9e3779b97f4a7c15ull + (lo << 6) + (lo >> 2);
    hi ^= h.hi + 0x9e3779b97f4a7c15ull + (hi << 6) + (hi >> 2);
}

static Hash combine_keys(std::span<const AssetKey> keys)
{
    uint64_t lo = 0xcbf29ce484222325ull;
    uint64_t hi = 0x100000001b3ull;

    for (const auto& k : keys) {
        mix_hash(lo, hi, k.content_hash);
        mix_hash(lo, hi, k.schema_hash);
        mix_hash(lo, hi, k.compiler_hash);
        mix_hash(lo, hi, k.deps_hash);
    }

    return { lo, hi };
}

static AssetKey make_file_key(std::span<const uint8_t> bytes, std::string_view path)
{
    return AssetKey{
        .content_hash = fnv1a(bytes),
        .schema_hash = hash_u64(wz::asset::kHLSLFileSchema.value),
        .compiler_hash = {},             // carrier nodes have no transform logic
        .deps_hash = hash_str(path), // disambiguates identical files at different paths
    };
}

// Returns FileResult<AssetNode> so the caller handles I/O errors before
// touching the asset system.
static wz::FileResult<AssetNode> make_file_node(
    const Path& path, SchemaID schema, AssetType type)
{
    auto file_result = read_file(path);
    if (!file_result)
        return { {}, file_result.error };

    Buffer& bytes = file_result.value;

    AssetNode node;
    node.key = make_file_key(bytes, path.c_str());
    node.type = type;
    node.schema = schema;
    node.stage = AssetStage::Source;
    node.payload = std::move(bytes);   // raw bytes live on the node

    return { std::move(node), wz::FileError::None };
}

static AssetKey make_shader_key(
    std::string_view name,
    std::span<const AssetKey> deps)
{
    return AssetKey{
        .content_hash = hash_str(name),
        .schema_hash = hash_u64(kHLSLSchema.value),
        .compiler_hash = hash_str("hlsl_compile_v1"),
        .deps_hash = combine_keys(deps),
    };
}


// ─── compile_failed_node ──────────────────────────────────────────────────────
// Returns the node unchanged (still Source stage) so resolve() sees
// stage != Compiled and produces ResolveError::CompileFailed.

static AssetNode compile_failed_node(const AssetNode& input) { return input; }

// ─── main ─────────────────────────────────────────────────────────────────────


int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // _CrtSetBreakAlloc(380);

    wz::Logger logger;
    logger.set_callback(wz::LogSinkType::Stderr);

    static constexpr char assets_dir[] =
        "D:\\code\\wozzits-window-engine\\window_engine\\resources\\shaders\\triangle";

    const Path assets_path{ assets_dir };

    wz::window::WindowDesc desc;
    desc.title = "Wozzits Window Test";
    desc.width = 800;
    desc.height = 600;

    wz::window::WindowHandle window = create_window(desc);

    wz::gpu::Device device =
        wz::gpu::create_device(window);

    wz::input::init_raw_input();


    // ── Compilers ─────────────────────────────────────────────────────────────
    //
    // File carrier compiler — preserves the byte payload so the shader
    // compiler's dep_nodes contain live bytes, not an overwritten handle.
    // Returns Compiled stage with an empty ResourceHandle (no GPU resource).

    CompilerRegistry registry;

    registry.register_compiler(AssetCompiler{
        .input_schema = kHLSLFileSchema,
        .output_type = AssetType::ShaderSource,
        .compile = [](const AssetNode& input, auto, auto) -> AssetNode {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            // payload intentionally stays as vector<uint8_t> — the shader
            // compiler reads bytes from dep_nodes, not dep_handles.
            return out;
        }
        });

    // Shader linker compiler — reads bytes from all file dep_nodes and
    // calls the GPU compiler. Stubbed here: just validates the bytes are
    // present and returns a sentinel handle so the test can run without GPU.
    registry.register_compiler(AssetCompiler{
        .input_schema = kHLSLSchema,
        .output_type = AssetType::Shader,
        .compile = [&logger, &device](
            const AssetNode& input,
            std::span<const AssetNode>   dep_nodes,
            std::span<const ResourceHandle>) -> AssetNode
        {
            const auto* desc = std::any_cast<wz::gpu::HLSLCompileDesc>(&input.meta);

            if (!desc) {
                logger.error("shader node missing HLSLCompileDesc");
                return compile_failed_node(input);
            }

            if (dep_nodes.empty()) {
                logger.error("shader node has no source dependencies");
                return compile_failed_node(input);
            }

            if (desc->primary_source_index >= dep_nodes.size()) {
                logger.error("shader primary_source_index out of range");
                return compile_failed_node(input);
            }

            std::vector<std::span<const uint8_t>> sources;
            sources.reserve(dep_nodes.size());

            for (const AssetNode& dep : dep_nodes) {
                const auto* bytes = std::get_if<std::vector<uint8_t>>(&dep.payload);

                if (!bytes) {
                    logger.error("file dep node has no byte payload");
                    return compile_failed_node(input);
                }

                sources.push_back({ bytes->data(), bytes->size() });
                logger.info("  source bytes available: "
                            + std::to_string(bytes->size()) + " bytes");
            }

            // --- GPU compile goes here: gpu.compile_hlsl(sources, *desc) ---
            // Stubbed for this test main: return a sentinel handle.
            // TODO: not implemented:
            wz::gpu::GPUHandle gpu_handle =
                wz::gpu::compile_hlsl(device, sources, *desc);

            if (!gpu_handle.valid()) {
                logger.error("gpu.compile_hlsl failed");
                return compile_failed_node(input);
            }


            logger.info("shader link stub: entry=" + desc->entry
                        + " target=" + desc->target
                        + " sources=" + std::to_string(sources.size()));

            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{
                .id = gpu_handle.id,
                .epoch = gpu_handle.epoch,
                .type = AssetType::Shader
            };
            return out;
        }
        });

    // ── Registration ──────────────────────────────────────────────────────────

    AssetSystem asset_sys(std::move(registry));

    const Path vs_path = wz::fs::join(assets_path, "triangle_vs.hlsl");
    const Path ps_path = wz::fs::join(assets_path, "triangle_ps.hlsl");

    auto vs_file = make_file_node(
        vs_path,
        kHLSLFileSchema,
        AssetType::ShaderSource
    );

    if (!vs_file) {
        logger.error("failed to read vertex shader: " + vs_path);
        return 1;
    }

    auto ps_file = make_file_node(
        ps_path,
        kHLSLFileSchema,
        AssetType::ShaderSource
    );

    if (!ps_file) {
        logger.error("failed to read pixel shader: " + ps_path);
        return 1;
    }

    AssetKey vs_file_key = vs_file.value.key;
    AssetKey ps_file_key = ps_file.value.key;

    asset_sys.register_asset(std::move(vs_file.value));
    asset_sys.register_asset(std::move(ps_file.value));


    AssetKey vs_deps[] = { vs_file_key };

    AssetKey vs_shader_key = make_shader_key("triangle_vs", vs_deps);

    AssetNode vs_shader_node;
    vs_shader_node.key = vs_shader_key;
    vs_shader_node.type = AssetType::Shader;
    vs_shader_node.schema = kHLSLSchema;
    vs_shader_node.stage = AssetStage::Source;
    vs_shader_node.payload = std::vector<uint8_t>{};
    vs_shader_node.meta = wz::gpu::HLSLCompileDesc{
        .stage = wz::gpu::ShaderStage::Vertex,
        .entry = "main",
        .target = "vs_5_0",
        .primary_source_index = 0,
    };

    asset_sys.register_asset(
        std::move(vs_shader_node),
        std::vector<AssetKey>{ vs_file_key }
    );

    AssetKey ps_deps[] = { ps_file_key };

    AssetKey ps_shader_key = make_shader_key("triangle_ps", ps_deps);

    AssetNode ps_shader_node;
    ps_shader_node.key = ps_shader_key;
    ps_shader_node.type = AssetType::Shader;
    ps_shader_node.schema = kHLSLSchema;
    ps_shader_node.stage = AssetStage::Source;
    ps_shader_node.payload = std::vector<uint8_t>{};
    ps_shader_node.meta = wz::gpu::HLSLCompileDesc{
        .stage = wz::gpu::ShaderStage::Pixel,
        .entry = "main",
        .target = "ps_5_0",
        .primary_source_index = 0,
    };

    asset_sys.register_asset(
        std::move(ps_shader_node),
        std::vector<AssetKey>{ ps_file_key }
    );

    // ── Commit ────────────────────────────────────────────────────────────────

    if (!asset_sys.commit()) {
        logger.error("asset graph rejected — cycle or missing dependency");
        return 1;
    }
    logger.info("asset graph committed");

    // ── Resolve ───────────────────────────────────────────────────────────────

    auto errors = std::vector<std::pair<AssetKey, ResolveError>>{};
    uint32_t resolved_count = asset_sys.resolve_all(&errors);

    logger.info("resolved asset count: " + std::to_string(resolved_count));

    auto shaders = asset_sys.query(AssetType::Shader, kHLSLSchema);

    if (shaders.size() != 2)
    {
        logger.error("expected exactly 2 compiled HLSL shaders, got "
            + std::to_string(shaders.size()));
        return 1;
    }

    wz::gpu::GPUHandle vs{};
    wz::gpu::GPUHandle ps{};

    for (const auto& shader : shaders)
    {
        const auto* desc =
            std::any_cast<wz::gpu::HLSLCompileDesc>(&shader.node->meta);

        if (!desc)
            continue;

        if (desc->stage == wz::gpu::ShaderStage::Vertex)
            vs = shader.handle;

        if (desc->stage == wz::gpu::ShaderStage::Pixel)
            ps = shader.handle;
    }

    assert(vs.valid());
    assert(ps.valid());

    wz::gpu::dx12::create_test_context(device, vs, ps);


    while (!window_should_close(window))
    {
        wz::window::pump_messages();

        PlatformEvent event{};
        while (poll_event(window, event))
        {
            if (event.type == PlatformEvent::Type::Resize)
            {
                wz::gpu::resize(
                    device,
                    event.resize.width,
                    event.resize.height
                );
            }

            if (event.type == PlatformEvent::Type::Close)
            {
                std::cout << "Close event\n";
            }
        }

        wz::gpu::begin_frame(device);
        wz::gpu::clear(device, 0.1f, 0.2f, 0.6f, 1.0f);

        // wz::gpu::dx12::draw_test_triangle(device);
        wz::gpu::dx12::draw_test_triangle_2(device);

        wz::gpu::end_frame(device);
        wz::gpu::present(device);
    }
    wz::input::shutdown_raw_input();
    wz::gpu::destroy_device(device);
    destroy_window(window);
    

    return 0;
}