#pragma once
// engine/assets/shader/importers.h

#include <expected>
#include <utility>
#include <asset/types.h>
#include <file/filesystem.h>
#include <file/result.h>

using namespace wz::asset;

namespace wz::asset::importer
{

    using FileNodeExpected = std::expected<std::pair<AssetKey, AssetNode>, FileError>;

    inline FileNodeExpected make_file_node(const Path& path, SchemaID schema, AssetType type) {
        auto result = read_file(path);
        if (!result) {
            return std::unexpected(result.error);
        }

        auto& bytes = result.value;
        AssetKey key = make_file_key(bytes, path.string());

        AssetNode node;
        node.key = key;
        node.type = type;
        node.schema = schema;
        node.stage = AssetStage::Source;
        node.payload = std::move(bytes);

        return std::pair{ key, std::move(node) };
    }
}