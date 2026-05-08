// tests/asset/foundational_assets.cpp

#include <gtest/gtest.h>

#include <asset/system.h>
#include <asset/compiler.h>
#include <asset/types.h>

#include <engine/assets/type_extensions.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/key_factories/file_carrier.h>
#include <engine/assets/engine_asset_library_internal.h>

#include <file/filesystem.h>

#include <logging/logger.h>

#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    using wz::asset::AssetType;

    inline constexpr wz::asset::SchemaID kTestTextConsumerSchema{
        0xF11E'CA55'E7'120001ull
    };

    inline constexpr wz::asset::AssetType kTestTextConsumerType =
        static_cast<wz::asset::AssetType>(60000);

    [[nodiscard]] wz::asset::AssetKey make_test_text_consumer_key(
        const wz::asset::AssetKey& text_file_key)
    {
        return wz::asset::AssetKey{
            .content_hash = { 0x1234, 0x5678 },
            .schema_hash =
                wz::engine::assets::detail::hash_u64(
                    kTestTextConsumerSchema.value),
            .compiler_hash = { 1, 0 },
            .deps_hash =
                wz::engine::assets::detail::key_to_dep_hash(text_file_key),
        };
    }
}

TEST(FoundationAssetTypes, StableNumericValues)
{
    using U = std::underlying_type_t<wz::asset::AssetType>;

    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeRawFile), static_cast<U>(64));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeTextFile), static_cast<U>(65));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeBinaryBlob), static_cast<U>(66));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeManifest), static_cast<U>(67));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeAssetBundle), static_cast<U>(68));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypePackage), static_cast<U>(69));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeImportedSourceFile), static_cast<U>(70));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeExternalReference), static_cast<U>(71));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeDirectory), static_cast<U>(72));
    EXPECT_EQ(static_cast<U>(wz::engine::assets::kAssetTypeArchive), static_cast<U>(73));
}

TEST(FoundationSchemas, StableNumericValues)
{
    EXPECT_EQ(wz::engine::assets::kRawFileSchema.value,
        0xF11E'CA55'E7'000001ull);

    EXPECT_EQ(wz::engine::assets::kHLSLFileSchema.value,
        0xF11E'CA55'E7'000002ull);

    EXPECT_EQ(wz::engine::assets::kTextFileSchema.value,
        0xF11E'CA55'E7'000003ull);

    EXPECT_EQ(wz::engine::assets::kBinaryBlobSchema.value,
        0xF11E'CA55'E7'000004ull);

    EXPECT_EQ(wz::engine::assets::kImportedSourceFileSchema.value,
        0xF11E'CA55'E7'000008ull);

    EXPECT_EQ(wz::engine::assets::kCustomBinaryFileSchema.value,
        0xF11E'CA55'E7'00000Full);

    EXPECT_EQ(wz::engine::assets::kJSONFileSchema.value,
        0xF11E'CA55'E7'00000Aull);
    
}

TEST(FoundationCarriers, CustomBinaryFileSchemaFeedsDependentRecipe)
{
    // ── Arrange file ─────────────────────────────────────────────────────────

    const wz::fs::Path root =
        wz::fs::join(wz::fs::temp_directory_path(),
            "wozzits_foundation_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path relative_path = "custom_format_payload.wzbin";
    const wz::fs::Path full_path = wz::fs::join(root, relative_path);

    const wz::fs::Buffer expected_bytes{
        'W', 'Z', 'B', 'N',
        0x01, 0x00, 0x00, 0x00,
        0x2A, 0x00, 0x00, 0x00
    };

    ASSERT_EQ(
        wz::fs::write_file(full_path, expected_bytes, true),
        wz::fs::FileError::None
    );

    // ── Arrange registry ─────────────────────────────────────────────────────

    wz::Logger logger;

    wz::asset::CompilerRegistry registry;

    wz::engine::assets::internal::register_file_carrier_compilers(
        registry,
        logger
    );

    registry.register_compiler(wz::asset::AssetCompiler{
        .input_schema = kTestTextConsumerSchema,
        .output_type = kTestTextConsumerType,
        .compile = [expected_bytes](
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
        {
            if (dep_nodes.size() != 1) {
                return input;
            }

            const auto* bytes =
                std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

            if (!bytes) {
                return input;
            }

            if (*bytes != expected_bytes) {
                return input;
            }

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = wz::asset::ResourceHandle{
                .id = 1,
                .epoch = 1,
                .type = kTestTextConsumerType,
            };

            return out;
        }
        });

    wz::asset::AssetSystem system(std::move(registry));

    // ── Register custom binary carrier node ──────────────────────────────────

    const std::string canonical =
        wz::engine::assets::detail::canonical_asset_path(relative_path);

    const wz::asset::AssetKey custom_binary_key =
        wz::engine::assets::make_file_key(
            canonical,
            wz::engine::assets::kCustomBinaryFileSchema
        );

    wz::asset::AssetNode custom_binary_node;
    custom_binary_node.key = custom_binary_key;
    custom_binary_node.type = wz::engine::assets::kAssetTypeBinaryBlob;
    custom_binary_node.schema = wz::engine::assets::kCustomBinaryFileSchema;
    custom_binary_node.stage = wz::asset::AssetStage::Source;
    custom_binary_node.payload = std::vector<uint8_t>{};
    custom_binary_node.meta = wz::engine::assets::internal::FileSourceDesc{
        .full_path = full_path,
        .canonical_path = canonical,
    };

    ASSERT_TRUE(system.register_asset(std::move(custom_binary_node)));

    // ── Register dependent recipe node ───────────────────────────────────────

    const wz::asset::AssetKey consumer_key =
        make_test_text_consumer_key(custom_binary_key);

    wz::asset::AssetNode consumer_node;
    consumer_node.key = consumer_key;
    consumer_node.type = kTestTextConsumerType;
    consumer_node.schema = kTestTextConsumerSchema;
    consumer_node.stage = wz::asset::AssetStage::Source;
    consumer_node.payload = std::vector<uint8_t>{};

    ASSERT_TRUE(system.register_asset(
        std::move(consumer_node),
        { custom_binary_key }
    ));

    // ── Act ──────────────────────────────────────────────────────────────────

    ASSERT_TRUE(system.commit());

    std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>> errors;
    const uint32_t resolved = system.resolve_all(&errors);

    // ── Assert ───────────────────────────────────────────────────────────────

    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(resolved, 2u);

    const auto* compiled = system.find_compiled(consumer_key);

    ASSERT_NE(compiled, nullptr);
    EXPECT_TRUE(compiled->handle.valid());
    EXPECT_EQ(compiled->handle.type, kTestTextConsumerType);
}

TEST(FoundationCarriers, ImportedSourceFileSchemaFeedsDependentRecipe)
{
    // ── Arrange file ─────────────────────────────────────────────────────────

    const wz::fs::Path root =
        wz::fs::join(wz::fs::temp_directory_path(),
            "wozzits_foundation_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path relative_path = "imported_source.meshsrc";
    const wz::fs::Path full_path = wz::fs::join(root, relative_path);

    const wz::fs::Buffer expected_bytes{
        'v', ' ', '0', ' ', '0', ' ', '0', '\n',
        'v', ' ', '1', ' ', '0', ' ', '0', '\n',
        'v', ' ', '0', ' ', '1', ' ', '0', '\n'
    };

    ASSERT_EQ(
        wz::fs::write_file(full_path, expected_bytes, true),
        wz::fs::FileError::None
    );

    // ── Arrange registry ─────────────────────────────────────────────────────

    wz::Logger logger;

    wz::asset::CompilerRegistry registry;

    wz::engine::assets::internal::register_file_carrier_compilers(
        registry,
        logger
    );

    registry.register_compiler(wz::asset::AssetCompiler{
        .input_schema = kTestTextConsumerSchema,
        .output_type = kTestTextConsumerType,
        .compile = [expected_bytes](
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
        {
            if (dep_nodes.size() != 1) {
                return input;
            }

            const auto* bytes =
                std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

            if (!bytes) {
                return input;
            }

            if (*bytes != expected_bytes) {
                return input;
            }

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = wz::asset::ResourceHandle{
                .id = 1,
                .epoch = 1,
                .type = kTestTextConsumerType,
            };

            return out;
        }
        });

    wz::asset::AssetSystem system(std::move(registry));

    // ── Register imported source file carrier node ───────────────────────────

    const std::string canonical =
        wz::engine::assets::detail::canonical_asset_path(relative_path);

    const wz::asset::AssetKey source_key =
        wz::engine::assets::make_file_key(
            canonical,
            wz::engine::assets::kImportedSourceFileSchema
        );

    wz::asset::AssetNode source_node;
    source_node.key = source_key;
    source_node.type = wz::engine::assets::kAssetTypeImportedSourceFile;
    source_node.schema = wz::engine::assets::kImportedSourceFileSchema;
    source_node.stage = wz::asset::AssetStage::Source;
    source_node.payload = std::vector<uint8_t>{};
    source_node.meta = wz::engine::assets::internal::FileSourceDesc{
        .full_path = full_path,
        .canonical_path = canonical,
    };

    ASSERT_TRUE(system.register_asset(std::move(source_node)));

    // ── Register dependent recipe node ───────────────────────────────────────

    const wz::asset::AssetKey consumer_key =
        make_test_text_consumer_key(source_key);

    wz::asset::AssetNode consumer_node;
    consumer_node.key = consumer_key;
    consumer_node.type = kTestTextConsumerType;
    consumer_node.schema = kTestTextConsumerSchema;
    consumer_node.stage = wz::asset::AssetStage::Source;
    consumer_node.payload = std::vector<uint8_t>{};

    ASSERT_TRUE(system.register_asset(
        std::move(consumer_node),
        { source_key }
    ));

    // ── Act ──────────────────────────────────────────────────────────────────

    ASSERT_TRUE(system.commit());

    std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>> errors;
    const uint32_t resolved = system.resolve_all(&errors);

    // ── Assert ───────────────────────────────────────────────────────────────

    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(resolved, 2u);

    const auto* compiled = system.find_compiled(consumer_key);

    ASSERT_NE(compiled, nullptr);
    EXPECT_TRUE(compiled->handle.valid());
    EXPECT_EQ(compiled->handle.type, kTestTextConsumerType);
}

TEST(FoundationCarriers, BinaryBlobSchemaFeedsDependentRecipe)
{
    // ── Arrange file ─────────────────────────────────────────────────────────

    const wz::fs::Path root =
        wz::fs::join(wz::fs::temp_directory_path(),
            "wozzits_foundation_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path relative_path = "hello_binary_blob.bin";
    const wz::fs::Path full_path = wz::fs::join(root, relative_path);

    const wz::fs::Buffer expected_bytes{
        0x00, 0x01, 0x02, 0x7F, 0x80, 0xFF
    };

    ASSERT_EQ(
        wz::fs::write_file(full_path, expected_bytes, true),
        wz::fs::FileError::None
    );

    wz::Logger logger;

    wz::asset::CompilerRegistry registry;

    wz::engine::assets::internal::register_file_carrier_compilers(
        registry,
        logger
    );

    registry.register_compiler(wz::asset::AssetCompiler{
        .input_schema = kTestTextConsumerSchema,
        .output_type = kTestTextConsumerType,
        .compile = [expected_bytes](
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
        {
            if (dep_nodes.size() != 1) {
                return input;
            }

            const auto* bytes =
                std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

            if (!bytes) {
                return input;
            }

            if (*bytes != expected_bytes) {
                return input;
            }

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = wz::asset::ResourceHandle{
                .id = 1,
                .epoch = 1,
                .type = kTestTextConsumerType,
            };

            return out;
        }
        });

    wz::asset::AssetSystem system(std::move(registry));

    // ── Register binary blob carrier node ────────────────────────────────────

    const std::string canonical =
        wz::engine::assets::detail::canonical_asset_path(relative_path);

    const wz::asset::AssetKey blob_key =
        wz::engine::assets::make_file_key(
            canonical,
            wz::engine::assets::kBinaryBlobSchema
        );

    wz::asset::AssetNode blob_node;
    blob_node.key = blob_key;
    blob_node.type = wz::engine::assets::kAssetTypeBinaryBlob;
    blob_node.schema = wz::engine::assets::kBinaryBlobSchema;
    blob_node.stage = wz::asset::AssetStage::Source;
    blob_node.payload = std::vector<uint8_t>{};
    blob_node.meta = wz::engine::assets::internal::FileSourceDesc{
        .full_path = full_path,
        .canonical_path = canonical,
    };

    ASSERT_TRUE(system.register_asset(std::move(blob_node)));

    // ── Register dependent recipe node ───────────────────────────────────────

    const wz::asset::AssetKey consumer_key =
        make_test_text_consumer_key(blob_key);

    wz::asset::AssetNode consumer_node;
    consumer_node.key = consumer_key;
    consumer_node.type = kTestTextConsumerType;
    consumer_node.schema = kTestTextConsumerSchema;
    consumer_node.stage = wz::asset::AssetStage::Source;
    consumer_node.payload = std::vector<uint8_t>{};

    ASSERT_TRUE(system.register_asset(
        std::move(consumer_node),
        { blob_key }
    ));

    // ── Act ──────────────────────────────────────────────────────────────────

    ASSERT_TRUE(system.commit());

    std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>> errors;
    const uint32_t resolved = system.resolve_all(&errors);

    // ── Assert ───────────────────────────────────────────────────────────────

    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(resolved, 2u);

    const auto* compiled = system.find_compiled(consumer_key);

    ASSERT_NE(compiled, nullptr);
    EXPECT_TRUE(compiled->handle.valid());
    EXPECT_EQ(compiled->handle.type, kTestTextConsumerType);
}

TEST(FoundationCarriers, TextFileSchemaFeedsDependentRecipe)
{
    // ── Arrange file ─────────────────────────────────────────────────────────

    const wz::fs::Path root =
        wz::fs::join(wz::fs::temp_directory_path(),
            "wozzits_foundation_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path relative_path = "hello_text_carrier.txt";
    const wz::fs::Path full_path = wz::fs::join(root, relative_path);

    ASSERT_EQ(
        wz::fs::write_file_text(full_path, "hello text carrier\n", true),
        wz::fs::FileError::None
    );

    // Use whatever logger construction your tests already use.
    // If Logger has no cheap default constructor, replace this with the same
    // logger helper used by the existing asset tests.
    wz::Logger logger;

    wz::asset::CompilerRegistry registry;

    wz::engine::assets::internal::register_file_carrier_compilers(
        registry,
        logger
    );

    registry.register_compiler(wz::asset::AssetCompiler{
        .input_schema = kTestTextConsumerSchema,
        .output_type = kTestTextConsumerType,
        .compile = [](
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
        {
            if (dep_nodes.size() != 1) {
                return input;
            }

            const auto* bytes =
                std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

            if (!bytes) {
                return input;
            }

            const std::string text(bytes->begin(), bytes->end());

            if (text != "hello text carrier\n") {
                return input;
            }

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = wz::asset::ResourceHandle{
                .id = 1,
                .epoch = 1,
                .type = kTestTextConsumerType,
            };

            return out;
        }
        });

    wz::asset::AssetSystem system(std::move(registry));

    // ── Register text carrier node ───────────────────────────────────────────

    const std::string canonical =
        wz::engine::assets::detail::canonical_asset_path(relative_path);

    const wz::asset::AssetKey text_key =
        wz::engine::assets::make_file_key(
            canonical,
            wz::engine::assets::kTextFileSchema
        );

    wz::asset::AssetNode text_node;
    text_node.key = text_key;
    text_node.type = wz::engine::assets::kAssetTypeTextFile;
    text_node.schema = wz::engine::assets::kTextFileSchema;
    text_node.stage = wz::asset::AssetStage::Source;
    text_node.payload = std::vector<uint8_t>{};
    text_node.meta = wz::engine::assets::internal::FileSourceDesc{
        .full_path = full_path,
        .canonical_path = canonical,
    };

    ASSERT_TRUE(system.register_asset(std::move(text_node)));

    // ── Register dependent recipe node ───────────────────────────────────────

    const wz::asset::AssetKey consumer_key =
        make_test_text_consumer_key(text_key);

    wz::asset::AssetNode consumer_node;
    consumer_node.key = consumer_key;
    consumer_node.type = kTestTextConsumerType;
    consumer_node.schema = kTestTextConsumerSchema;
    consumer_node.stage = wz::asset::AssetStage::Source;
    consumer_node.payload = std::vector<uint8_t>{};

    ASSERT_TRUE(system.register_asset(
        std::move(consumer_node),
        { text_key }
    ));

    // ── Act ──────────────────────────────────────────────────────────────────

    ASSERT_TRUE(system.commit());

    std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>> errors;
    const uint32_t resolved = system.resolve_all(&errors);

    // ── Assert ───────────────────────────────────────────────────────────────

    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(resolved, 2u);

    const auto* compiled = system.find_compiled(consumer_key);

    ASSERT_NE(compiled, nullptr);
    EXPECT_TRUE(compiled->handle.valid());
    EXPECT_EQ(compiled->handle.type, kTestTextConsumerType);
}