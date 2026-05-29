// tests/asset/vector_field_tests.cpp

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/vector_field/vector_field.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

namespace wz::engine::assets::test
{
    namespace fs = std::filesystem;

    static bool write_raw_f32(
        const wz::fs::Path& path,
        const std::vector<float>& values)
    {
        const size_t byte_count = values.size() * sizeof(float);
        wz::fs::Buffer bytes(byte_count);
        std::memcpy(bytes.data(), values.data(), byte_count);
        return wz::fs::write_file(path, bytes, true) == wz::fs::FileError::None;
    }

    class VectorFieldTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            temp_dir_ = wz::fs::Path{
                (fs::temp_directory_path() / "wz_vector_field_tests").string()
            };
            fs::create_directories(temp_dir_);

            library_ = std::make_unique<EngineAssetLibrary>(
                null_device_,
                logger_,
                temp_dir_);
        }

        void TearDown() override
        {
            library_.reset();
            fs::remove_all(temp_dir_);
        }

        wz::fs::Path write_field_file(
            const std::string& name,
            const std::vector<float>& values)
        {
            const wz::fs::Path rel{ name };
            const wz::fs::Path full = wz::fs::join(temp_dir_, rel);
            EXPECT_TRUE(write_raw_f32(full, values));
            return rel;
        }

        wz::gpu::Device null_device_{};
        wz::Logger logger_{};
        wz::fs::Path temp_dir_{};
        std::unique_ptr<EngineAssetLibrary> library_;
    };

    TEST(VectorFieldAssetTests, DefaultVectorFieldAssetIsInvalid)
    {
        VectorFieldAsset asset{};
        EXPECT_FALSE(asset.valid());
    }

    TEST_F(VectorFieldTest, RawF32NormalFieldResolves)
    {
        // Two samples, one "normal" channel, three components per channel.
        const std::vector<float> values{
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f,
        };
        const auto rel = write_field_file("normal_2x1.rawf32", values);

        const VectorFieldAsset asset =
            library_->vector_fields().create_vector_field({
                .name = "normal_2x1",
                .path = rel,
                .width = 2,
                .height = 1,
                .depth = 1,
                .components_per_channel = 3,
                .channels = { VectorFieldChannelDesc{ .name = "normal" } },
                .format = VectorFieldFormat::Float32,
                .domain_kind = VectorFieldDomainKind::Spatial2D,
            });
        ASSERT_TRUE(asset.valid());

        ASSERT_TRUE(library_->commit());
        ASSERT_TRUE(library_->resolve_all().ok());

        const VectorFieldHandle handle =
            library_->vector_fields().get_vector_field(asset);
        ASSERT_TRUE(handle.valid());

        const VectorFieldData* data =
            library_->vector_fields().get_vector_field_data(handle);
        ASSERT_NE(data, nullptr);
        ASSERT_TRUE(data->valid());

        EXPECT_EQ(data->width, 2u);
        EXPECT_EQ(data->height, 1u);
        EXPECT_EQ(data->depth, 1u);
        EXPECT_EQ(data->channel_count(), 1u);
        EXPECT_EQ(data->components_per_channel, 3u);
        ASSERT_EQ(data->channels.size(), 1u);
        EXPECT_EQ(data->channels[0].name, "normal");

        EXPECT_FLOAT_EQ(data->at(0, 0, 0, 0, 0), 0.0f);
        EXPECT_FLOAT_EQ(data->at(0, 0, 0, 0, 1), 1.0f);
        EXPECT_FLOAT_EQ(data->at(0, 0, 0, 0, 2), 0.0f);
        EXPECT_FLOAT_EQ(data->at(1, 0, 0, 0, 0), 0.0f);
        EXPECT_FLOAT_EQ(data->at(1, 0, 0, 0, 1), 0.0f);
        EXPECT_FLOAT_EQ(data->at(1, 0, 0, 0, 2), 1.0f);

        ASSERT_EQ(data->min_values.size(), 3u);
        ASSERT_EQ(data->max_values.size(), 3u);
        EXPECT_FLOAT_EQ(data->min_values[0], 0.0f);
        EXPECT_FLOAT_EQ(data->min_values[1], 0.0f);
        EXPECT_FLOAT_EQ(data->min_values[2], 0.0f);
        EXPECT_FLOAT_EQ(data->max_values[0], 0.0f);
        EXPECT_FLOAT_EQ(data->max_values[1], 1.0f);
        EXPECT_FLOAT_EQ(data->max_values[2], 1.0f);
    }

    TEST_F(VectorFieldTest, MultiChannelVectorFieldUsesSampleChannelComponentLayout)
    {
        // One sample, two flow-like channels, two components each.
        const std::vector<float> values{
            1.0f, 2.0f,
            3.0f, 4.0f,
        };
        const auto rel = write_field_file("flow_layers.rawf32", values);

        const VectorFieldAsset asset =
            library_->vector_fields().create_vector_field({
                .name = "flow_layers",
                .path = rel,
                .width = 1,
                .height = 1,
                .depth = 1,
                .components_per_channel = 2,
                .channels = {
                    VectorFieldChannelDesc{ .name = "surface_flow" },
                    VectorFieldChannelDesc{ .name = "subsurface_flow" },
                },
            });
        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        ASSERT_TRUE(library_->resolve_all().ok());

        const VectorFieldData* data =
            library_->vector_fields().get_vector_field_data(
                library_->vector_fields().get_vector_field(asset));
        ASSERT_NE(data, nullptr);

        EXPECT_FLOAT_EQ(data->at(0, 0, 0, 0, 0), 1.0f);
        EXPECT_FLOAT_EQ(data->at(0, 0, 0, 0, 1), 2.0f);
        EXPECT_FLOAT_EQ(data->at(0, 0, 0, 1, 0), 3.0f);
        EXPECT_FLOAT_EQ(data->at(0, 0, 0, 1, 1), 4.0f);
    }

    TEST_F(VectorFieldTest, ComponentCountOneIsRejectedForVectorFieldV1)
    {
        const auto rel = write_field_file("scalar_shape.rawf32", { 1.0f });

        const VectorFieldAsset asset =
            library_->vector_fields().create_vector_field({
                .name = "scalar_shape",
                .path = rel,
                .width = 1,
                .height = 1,
                .depth = 1,
                .components_per_channel = 1,
                .channels = { VectorFieldChannelDesc{ .name = "height" } },
            });
        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const VectorFieldHandle handle =
            library_->vector_fields().get_vector_field(asset);
        EXPECT_FALSE(handle.valid());
    }

    TEST_F(VectorFieldTest, WrongByteCountIsRejected)
    {
        const auto rel = write_field_file("short.rawf32", { 1.0f, 2.0f });

        const VectorFieldAsset asset =
            library_->vector_fields().create_vector_field({
                .name = "short",
                .path = rel,
                .width = 2,
                .height = 1,
                .depth = 1,
                .components_per_channel = 3,
                .channels = { VectorFieldChannelDesc{ .name = "normal" } },
            });
        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const VectorFieldHandle handle =
            library_->vector_fields().get_vector_field(asset);
        EXPECT_FALSE(handle.valid());
    }

    TEST_F(VectorFieldTest, ChannelNamesContributeToIdentity)
    {
        const std::vector<float> values{ 1.0f, 2.0f };
        const auto rel = write_field_file("identity.rawf32", values);

        const VectorFieldAsset flow =
            library_->vector_fields().create_vector_field({
                .name = "field",
                .path = rel,
                .width = 1,
                .height = 1,
                .depth = 1,
                .components_per_channel = 2,
                .channels = { VectorFieldChannelDesc{ .name = "flow" } },
            });
        const VectorFieldAsset normal_xy =
            library_->vector_fields().create_vector_field({
                .name = "field",
                .path = rel,
                .width = 1,
                .height = 1,
                .depth = 1,
                .components_per_channel = 2,
                .channels = { VectorFieldChannelDesc{ .name = "normal_xy" } },
            });

        ASSERT_TRUE(flow.valid());
        ASSERT_TRUE(normal_xy.valid());
        EXPECT_NE(flow.output, normal_xy.output);
    }

    TEST(VectorFieldTableTests, DestroyRestoresNullSentinel)
    {
        VectorFieldTable table;

        VectorFieldData field;
        field.width = 1;
        field.height = 1;
        field.depth = 1;
        field.components_per_channel = 2;
        field.channels = { VectorFieldChannelDesc{ .name = "flow" } };
        field.min_values = { 1.0f, 2.0f };
        field.max_values = { 1.0f, 2.0f };
        field.values = { 1.0f, 2.0f };

        auto h1 = table.add(field);
        EXPECT_TRUE(h1.valid());
        EXPECT_NE(h1.id, 0u);

        table.destroy();

        auto h2 = table.add(field);
        EXPECT_TRUE(h2.valid());
        EXPECT_NE(h2.id, 0u);
    }

} // namespace wz::engine::assets::test
