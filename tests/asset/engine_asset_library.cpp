#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <gpu/gpu.h>
#include <window/window2.h>

#include <filesystem>
#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
    namespace fs = std::filesystem;

    struct TempResourceDir
    {
        fs::path root;

        TempResourceDir()
        {
            root = fs::temp_directory_path() /
                ("wozzits_asset_library_test_" + std::to_string(::GetCurrentProcessId()));

            fs::remove_all(root);
            fs::create_directories(root / "shaders" / "triangle");
        }

        ~TempResourceDir()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        fs::path shader_dir() const
        {
            return root / "shaders" / "triangle";
        }

        wz::fs::Path wz_root() const
        {
            return root.string();
        }
    };

    static void write_text_file(const fs::path& path, const std::string& text)
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.good()) << "failed to open " << path.string();
        out << text;
    }

    static void write_triangle_shaders(const TempResourceDir& dir)
    {
        write_text_file(
            dir.shader_dir() / "triangle_vs.hlsl",
            R"(
cbuffer TransformCB : register(b0)
{
    float4x4 view_proj;
};

struct VSIn
{
    float3 pos : POSITION;
};

struct PSIn
{
    float4 pos : SV_POSITION;
};

PSIn main(VSIn input)
{
    PSIn output;
    output.pos = mul(view_proj, float4(input.pos, 1.0f));
    return output;
}
)"
);

        write_text_file(
            dir.shader_dir() / "triangle_ps.hlsl",
            R"(
struct PSIn
{
    float4 pos : SV_POSITION;
};

float4 main(PSIn input) : SV_TARGET
{
    return float4(1.0f, 0.2f, 0.1f, 1.0f);
}
)"
);
    }

    // Minimal real GPU fixture.
    //
    // These tests compile HLSL through the real GPU path, so they need a window
    // and device. Keep them in the window/gpu test group, not in a pure asset
    // library unit-test group.
    struct AssetLibraryGpuFixture : public ::testing::Test
    {
        wz::Logger logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};

        void SetUp() override
        {
            logger.set_callback(wz::LogSinkType::Stderr);

            wz::window::WindowDesc desc{};
            desc.title = "asset_library_test";
            desc.width = 64;
            desc.height = 64;
            desc.resizable = false;

            window = wz::window::create_window(desc);
            ASSERT_TRUE(window.native);

            device = wz::gpu::create_device(window);
            ASSERT_TRUE(device.impl);
        }

        void TearDown() override
        {
            if (device.impl)
                wz::gpu::destroy_device(device);

            if (window.native)
                wz::window::destroy_window(window);
        }
    };
}

TEST(EngineAssetLibrary, DefaultShaderPairAssetIsInvalid)
{
    wz::engine::assets::ShaderPairAsset asset{};
    EXPECT_FALSE(asset.valid());
}

TEST_F(AssetLibraryGpuFixture, CreateShaderPairReturnsValidAssetKeys)
{
    TempResourceDir resources;
    write_triangle_shaders(resources);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.create_shader_pair({
        .name = "triangle",
        .vertex_path = "shaders/triangle/triangle_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    EXPECT_TRUE(pair.valid());
}

TEST_F(AssetLibraryGpuFixture, CommitSucceedsAfterShaderPairRegistration)
{
    TempResourceDir resources;
    write_triangle_shaders(resources);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.create_shader_pair({
        .name = "triangle",
        .vertex_path = "shaders/triangle/triangle_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    ASSERT_TRUE(pair.valid());
    EXPECT_TRUE(assets.commit());
}

TEST_F(AssetLibraryGpuFixture, ResolveShaderPairProducesValidHandles)
{
    TempResourceDir resources;
    write_triangle_shaders(resources);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.create_shader_pair({
        .name = "triangle",
        .vertex_path = "shaders/triangle/triangle_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    ASSERT_TRUE(pair.valid());
    ASSERT_TRUE(assets.commit());

    const uint32_t resolved_count = assets.resolve_all();
    EXPECT_GE(resolved_count, 4u);
    // Expected nodes:
    // - VS file carrier
    // - PS file carrier
    // - VS shader
    // - PS shader

    auto handles = assets.get_shader_pair(pair);

    EXPECT_TRUE(handles.valid());
    EXPECT_TRUE(handles.vertex.valid());
    EXPECT_TRUE(handles.pixel.valid());
}

TEST_F(AssetLibraryGpuFixture, MissingShaderFileDoesNotProduceValidHandles)
{
    TempResourceDir resources;

    // Only write pixel shader. Vertex shader is intentionally missing.
    write_text_file(
        resources.shader_dir() / "triangle_ps.hlsl",
        R"(
struct PSIn
{
    float4 pos : SV_POSITION;
};

float4 main(PSIn input) : SV_TARGET
{
    return float4(1.0f, 0.2f, 0.1f, 1.0f);
}
)"
);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.create_shader_pair({
        .name = "triangle",
        .vertex_path = "shaders/triangle/missing_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    ASSERT_TRUE(pair.valid());
    ASSERT_TRUE(assets.commit());

    assets.resolve_all();

    auto handles = assets.get_shader_pair(pair);

    EXPECT_FALSE(handles.valid());
    EXPECT_FALSE(handles.vertex.valid());
}

TEST_F(AssetLibraryGpuFixture, InvalidHlslDoesNotProduceValidShaderPair)
{
    TempResourceDir resources;

    write_text_file(
        resources.shader_dir() / "triangle_vs.hlsl",
        "this is not valid hlsl"
    );

    write_text_file(
        resources.shader_dir() / "triangle_ps.hlsl",
        R"(
float4 main() : SV_TARGET
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
)"
);

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        resources.wz_root()
    };

    auto pair = assets.create_shader_pair({
        .name = "bad_triangle",
        .vertex_path = "shaders/triangle/triangle_vs.hlsl",
        .pixel_path = "shaders/triangle/triangle_ps.hlsl",
        });

    ASSERT_TRUE(pair.valid());
    ASSERT_TRUE(assets.commit());

    assets.resolve_all();

    auto handles = assets.get_shader_pair(pair);

    EXPECT_FALSE(handles.valid());
    EXPECT_FALSE(handles.vertex.valid());
}

