#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <fstream>
#include <sstream>
#include <string>

namespace
{
    wz::engine::assets::DataTableData make_source_table()
    {
        wz::engine::assets::DataTableData table;
        table.columns.push_back({ .name = "name" });
        table.columns.push_back({ .name = "value" });

        table.rows.push_back({ .cells = { "alpha", "1" } });
        table.rows.push_back({ .cells = { "beta",  "2" } });
        table.rows.push_back({ .cells = { "gamma", "3" } });

        return table;
    }

    std::string read_text_file(const std::string& path)
    {
        std::ifstream f(path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
}

TEST(CSVExportAssetModule, WritesCSVFile)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_csv_export_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    const auto table_asset =
        assets.data_tables().create_inline_table({
            .name  = "csv_export/source_table",
            .table = make_source_table(),
        });

    ASSERT_TRUE(table_asset.valid());

    const wz::fs::Path output_path =
        wz::fs::join(root, "export_test.csv");

    const auto export_asset =
        assets.csv_export().create_csv_export({
            .name         = "csv_export/export_test",
            .source       = table_asset,
            .output_path  = output_path,
            .separator    = ',',
            .include_header = true,
        });

    ASSERT_TRUE(export_asset.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle = assets.csv_export().get_export(export_asset);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, wz::engine::assets::kAssetTypeCSVExport);

    const auto* data = assets.csv_export().get_export_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->output_path, output_path);
    EXPECT_EQ(data->column_count, 2u);
    EXPECT_EQ(data->row_count, 3u);

    ASSERT_TRUE(wz::fs::exists(output_path));

    const std::string csv = read_text_file(output_path);

    EXPECT_NE(csv.find("name,value"), std::string::npos);
    EXPECT_NE(csv.find("alpha,1"),    std::string::npos);
    EXPECT_NE(csv.find("beta,2"),     std::string::npos);
    EXPECT_NE(csv.find("gamma,3"),    std::string::npos);
}

TEST(CSVExportAssetModule, WritesCSVWithoutHeader)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_csv_export_no_header_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    const auto table_asset =
        assets.data_tables().create_inline_table({
            .name  = "csv_export/source_table_no_header",
            .table = make_source_table(),
        });

    ASSERT_TRUE(table_asset.valid());

    const wz::fs::Path output_path =
        wz::fs::join(root, "export_no_header.csv");

    const auto export_asset =
        assets.csv_export().create_csv_export({
            .name           = "csv_export/export_no_header",
            .source         = table_asset,
            .output_path    = output_path,
            .separator      = ',',
            .include_header = false,
        });

    ASSERT_TRUE(export_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    ASSERT_TRUE(wz::fs::exists(output_path));

    const std::string csv = read_text_file(output_path);

    EXPECT_EQ(csv.find("name,value"), std::string::npos);
    EXPECT_NE(csv.find("alpha,1"), std::string::npos);
}

TEST(CSVExportAssetModule, EscapesFieldsContainingSeparator)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_csv_export_escape_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    wz::engine::assets::DataTableData table;
    table.columns.push_back({ .name = "description" });
    table.columns.push_back({ .name = "value" });
    table.rows.push_back({ .cells = { "hello, world", "42" } });
    table.rows.push_back({ .cells = { "say \"hi\"",   "99" } });

    const auto table_asset =
        assets.data_tables().create_inline_table({
            .name  = "csv_export/escape_table",
            .table = table,
        });

    ASSERT_TRUE(table_asset.valid());

    const wz::fs::Path output_path =
        wz::fs::join(root, "export_escape.csv");

    const auto export_asset =
        assets.csv_export().create_csv_export({
            .name        = "csv_export/export_escape",
            .source      = table_asset,
            .output_path = output_path,
        });

    ASSERT_TRUE(export_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    ASSERT_TRUE(wz::fs::exists(output_path));

    const std::string csv = read_text_file(output_path);

    // Fields with commas must be quoted
    EXPECT_NE(csv.find("\"hello, world\""), std::string::npos);
    // Internal double-quotes must be doubled
    EXPECT_NE(csv.find("\"say \"\"hi\"\"\""), std::string::npos);
}

TEST(CSVExportAssetModule, RejectsEmptyOutputPath)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_csv_export_invalid_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    const auto table_asset =
        assets.data_tables().create_inline_table({
            .name  = "csv_export/reject_table",
            .table = make_source_table(),
        });

    ASSERT_TRUE(table_asset.valid());

    const auto export_asset =
        assets.csv_export().create_csv_export({
            .name        = "csv_export/reject_export",
            .source      = table_asset,
            .output_path = "",
        });

    EXPECT_FALSE(export_asset.valid());
}
