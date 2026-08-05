#pragma once

#include <asset/draft.h>
#include <file/filesystem.h>

namespace wz::engine::assets
{
    [[nodiscard]] bool engine_key_factory_handles(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type) noexcept;

    // `resource_root` is the project directory a RELATIVE file-carrier source
    // path is read against when deciding a carrier's key — mirroring the asset
    // compilers (35fb5a08). Without it the read resolves against the process
    // working directory, so the content-vs-path-only key SHAPE (and thus the
    // asset key, and every disk-cache filename derived from it) depended on where
    // the process was launched. Empty (the default) preserves the old
    // CWD-relative behavior for callers that have no resource root (unit tests).
    [[nodiscard]] wz::asset::AssetKeyFactoryFn make_engine_asset_key_factory(
        const wz::asset::CompilerRegistry& registry,
        const wz::fs::Path& resource_root = {});
}
