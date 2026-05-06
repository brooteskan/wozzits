// gpu/scalar_field_texture_tests.cpp

#include <gtest/gtest.h>

#include <gpu/gpu.h>
#include <gpu/scalar_field_texture.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <window/window2.h>

namespace wz::gpu::test
{
    TEST(ScalarFieldTextureTests, UploadScalarFieldTextureReturnsValidHandle)
    {
        // Create a tiny hidden/test window using whatever window helper your GPU
        // tests already use.
        wz::window::WindowHandle window =
            wz::window::create_window({
                .title = "scalar field texture upload test",
                .width = 64,
                .height = 64,
                .resizable = false,
                });

        ASSERT_TRUE(window.valid());

        wz::gpu::Device device = wz::gpu::create_device(window);
        ASSERT_TRUE(device.valid());

        wz::engine::assets::ScalarFieldData field{};
        field.width = 4;
        field.height = 4;
        field.depth = 1;
        field.format = wz::engine::assets::ScalarFieldFormat::Float32;
        field.domain_kind = wz::engine::assets::ScalarFieldDomainKind::Spatial2D;
        field.values.resize(16);

        for (uint32_t y = 0; y < field.height; ++y) {
            for (uint32_t x = 0; x < field.width; ++x) {
                field.values[x + y * field.width] =
                    static_cast<float>(x) / static_cast<float>(field.width - 1);
            }
        }

        field.min_value = 0.0f;
        field.max_value = 1.0f;

        ASSERT_TRUE(field.valid());

        const wz::gpu::GPUHandle texture =
            wz::gpu::upload_scalar_field_texture(device, field);

        EXPECT_TRUE(texture.valid());
        EXPECT_EQ(texture.type, wz::gpu::GPUResourceType::Texture);

        wz::gpu::destroy_device(device);
        wz::window::destroy_window(window);
    }
}