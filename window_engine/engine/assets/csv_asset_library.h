#pragma once

// engine/assets/csv/csv_asset_library.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>

#include <string>

namespace wz::engine::assets
{
    struct CSVDocument;

    // CSV v1 target:
    //   - RFC 4180-style comma-separated records
    //   - quoted fields with "" quote escaping
    //   - multiline quoted fields allowed
    //   - CRLF and LF accepted
    //   - ragged rows allowed
    //   - no type inference; cells remain text
    struct CSVFileDesc
    {
        std::string name;

        // Relative to the owning asset library's resource root.
        wz::fs::Path path;

        // CSV means comma-separated. General delimiter support can become
        // DelimitedTextAssetLibrary later.
        char delimiter = ',';

        bool allow_ragged_rows = true;
    };

    struct CSVFileAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct CSVDocumentHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class CSVAssetLibrary
    {
    public:
        CSVAssetLibrary(
            wz::asset::AssetSystem& system,
            wz::fs::Path resource_root
        );

        CSVAssetLibrary(const CSVAssetLibrary&) = delete;
        CSVAssetLibrary& operator=(const CSVAssetLibrary&) = delete;

        CSVAssetLibrary(CSVAssetLibrary&&) = delete;
        CSVAssetLibrary& operator=(CSVAssetLibrary&&) = delete;

        // Register a CSV file asset in the shared AssetSystem.
        // The owning system must still be committed/resolved by the caller.
        CSVFileAsset create_csv_file(const CSVFileDesc& desc);

        // Retrieve the ResourceHandle for a resolved CSV document asset.
        // Returns an invalid handle if the asset has not been resolved.
        CSVDocumentHandle get_csv_file(const CSVFileAsset& asset) const;

        // Retrieve parsed CSV data by handle.
        // This will be implemented once CSVDocumentTable exists.
        const CSVDocument* get_csv_document_data(
            CSVDocumentHandle handle) const;

    private:
        wz::asset::AssetKey register_csv_file_node(
            wz::asset::AssetKey source_file,
            const CSVFileDesc& desc
        );

    private:
        wz::asset::AssetSystem& system_;
        wz::fs::Path resource_root_;
    };

} // namespace wz::engine::assets