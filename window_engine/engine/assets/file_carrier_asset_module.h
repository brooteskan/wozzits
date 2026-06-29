#pragma once

// engine/assets/file_carrier_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>

#include <logging/logger.h>

namespace wz::engine::assets
{
    // Resolve a file-carrier path against a resource root, mirroring how source
    // nodes are rooted: strip a single matched surrounding pair of ASCII
    // double-quotes (Windows Explorer's "Copy as path" wraps the path), then
    // return the path unchanged when absolute, or joined onto `resource_root`
    // when relative. This is the single engine-side authority for the rooting
    // convention so callers (FileCarrierAssetModule, the editor ABI's GLB
    // import) never reimplement it.
    [[nodiscard]] wz::fs::Path resolve_file_carrier_path(
        const wz::fs::Path& resource_root,
        const wz::fs::Path& path);

    // Manages file-backed source node registration in the shared AssetSystem.
    //
    // This module is implementation plumbing shared by ShaderAssetModule,
    // ScalarFieldAssetModule, and any future module that needs to register
    // file-backed carrier nodes. It does not expose a public creation API —
    // it exists so that file-node logic is not duplicated across modules.
    //
    // Paths passed to register_file_node() may be relative to the resource
    // root provided at construction, or absolute paths that already identify
    // the source file. The module canonicalises paths and constructs keys
    // before forwarding to the shared AssetSystem.

    class FileCarrierAssetModule
    {
    public:
        FileCarrierAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            wz::fs::Path            resource_root
        );

        // Register a file-backed source node in the shared AssetSystem.
        // path is canonicalised. Relative paths are joined with the resource
        // root; absolute paths are used directly.
        // Returns the node key, or a zero key on failure.
        [[nodiscard]] wz::asset::AssetKey register_file_node(
            const wz::fs::Path& path,
            wz::asset::SchemaID  schema,
            wz::asset::AssetType type
        );

        [[nodiscard]] wz::fs::Path resolve_path(
            const wz::fs::Path& path) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        wz::fs::Path            resource_root_;
    };

} // namespace wz::engine::assets
