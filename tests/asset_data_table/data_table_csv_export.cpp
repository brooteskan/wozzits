#include <gtest/gtest.h>

#include <engine/assets/data_table_csv_export.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <string>

namespace
{
    using wz::engine::assets::DataTableCsvExportStatus;

    // frame | frame_ms, frames 0-2
    wz::engine::assets::DataTableData make_table()
    {
        wz::engine::assets::DataTableData table;
        table.columns.push_back({ .name = "frame"    });
        table.columns.push_back({ .name = "frame_ms" });

        for (uint32_t f = 0; f < 3; ++f) {
            table.rows.push_back({ .cells = {
                std::to_string(f),
                std::to_string(f) + ".5",
            }});
        }

        return table;
    }

    wz::fs::Path make_root(const std::string& leaf)
    {
        const wz::fs::Path root =
            wz::fs::join(wz::fs::temp_directory_path(), leaf);

        // create_directories is idempotent; a previous run's tree is fine since
        // every case writes its own file name.
        EXPECT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);
        return root;
    }
}

TEST(DataTableCsvExport, WritesTableToFile)
{
    const wz::fs::Path root = make_root("wozzits_data_table_csv_export_tests");
    const wz::fs::Path out  = wz::fs::join(root, "written.csv");

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    EXPECT_EQ(
        wz::engine::assets::write_data_table_to_csv_file(
            assets, "profile/written", make_table(), out),
        DataTableCsvExportStatus::Ok);

    ASSERT_TRUE(wz::fs::exists(out));

    const auto text = wz::fs::read_file_text(out);
    ASSERT_TRUE(text);

    // Header included and comma-separated (the export node is created with
    // CSVExportDesc's defaults), CRLF line endings.
    EXPECT_EQ(text.value, "frame,frame_ms\r\n0,0.5\r\n1,1.5\r\n2,2.5\r\n");
}

TEST(DataTableCsvExport, RegistersTableAndExportNodes)
{
    const wz::fs::Path root = make_root("wozzits_data_table_csv_export_nodes_tests");
    const wz::fs::Path out  = wz::fs::join(root, "nodes.csv");

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const size_t before = assets.system().registered_assets().size();

    ASSERT_EQ(
        wz::engine::assets::write_data_table_to_csv_file(
            assets, "profile/nodes", make_table(), out),
        DataTableCsvExportStatus::Ok);

    // The inline table node plus the export node that reads it -- and the export
    // resolved, which is what separates Ok from ExportUnresolved.
    EXPECT_EQ(assets.system().registered_assets().size(), before + 2u);
}

TEST(DataTableCsvExport, InvalidTableIsRejectedAndWritesNothing)
{
    const wz::fs::Path root = make_root("wozzits_data_table_csv_export_ragged_tests");
    const wz::fs::Path out  = wz::fs::join(root, "ragged.csv");

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    // Ragged: a row whose cell count disagrees with the column count fails
    // DataTableData::valid(), so the inline table node is never created.
    wz::engine::assets::DataTableData ragged = make_table();
    ragged.rows.push_back({ .cells = { "3" } });

    EXPECT_EQ(
        wz::engine::assets::write_data_table_to_csv_file(
            assets, "profile/ragged", std::move(ragged), out),
        DataTableCsvExportStatus::TableInvalid);

    EXPECT_FALSE(wz::fs::exists(out));
}

TEST(DataTableCsvExport, EmptyNameIsRejected)
{
    const wz::fs::Path root = make_root("wozzits_data_table_csv_export_noname_tests");
    const wz::fs::Path out  = wz::fs::join(root, "noname.csv");

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    // An empty name fails at the same stage as an invalid table: the inline
    // table node needs a name to key on.
    EXPECT_EQ(
        wz::engine::assets::write_data_table_to_csv_file(
            assets, "", make_table(), out),
        DataTableCsvExportStatus::TableInvalid);

    EXPECT_FALSE(wz::fs::exists(out));
}

TEST(DataTableCsvExport, UnwritablePathReportsWriteFailed)
{
    const wz::fs::Path root = make_root("wozzits_data_table_csv_export_badpath_tests");

    // A directory that was never created -- the export itself resolves fine, so
    // this reaches the write stage and fails only there.
    const wz::fs::Path out =
        wz::fs::join(wz::fs::join(root, "no_such_dir"), "unwritable.csv");

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    EXPECT_EQ(
        wz::engine::assets::write_data_table_to_csv_file(
            assets, "profile/badpath", make_table(), out),
        DataTableCsvExportStatus::WriteFailed);

    EXPECT_FALSE(wz::fs::exists(out));
}

TEST(DataTableCsvExport, EmptyRowsWritesHeaderOnly)
{
    const wz::fs::Path root = make_root("wozzits_data_table_csv_export_header_tests");
    const wz::fs::Path out  = wz::fs::join(root, "header_only.csv");

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    // Columns but no rows is a valid table -- a profiler flushed before any
    // frame landed. It should still produce a well-formed header.
    wz::engine::assets::DataTableData table = make_table();
    table.rows.clear();

    ASSERT_EQ(
        wz::engine::assets::write_data_table_to_csv_file(
            assets, "profile/header_only", std::move(table), out),
        DataTableCsvExportStatus::Ok);

    const auto text = wz::fs::read_file_text(out);
    ASSERT_TRUE(text);
    EXPECT_EQ(text.value, "frame,frame_ms\r\n");
}
