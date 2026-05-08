#pragma once

// engine/assets/file_carrier_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>

#include <logging/logger.h>

namespace wz::engine::assets
{
    // Manages file-backed source node registration in the shared AssetSystem.
    //
    // This module is implementation plumbing shared by ShaderAssetModule,
    // ScalarFieldAssetModule, and any future module that needs to register
    // file-backed carrier nodes. It does not expose a public creation API —
    // it exists so that file-node logic is not duplicated across modules.
    //
    // All paths passed to register_file_node() are relative to the resource
    // root provided at construction. The module canonicalises paths and
    // constructs keys before forwarding to the shared AssetSystem.

    class FileCarrierAssetModule
    {
    public:
        FileCarrierAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            wz::fs::Path            resource_root
        );

        // Register a file-backed source node in the shared AssetSystem.
        // relative_path is canonicalised and joined with the resource root.
        // Returns the node key, or a zero key on failure.
        [[nodiscard]] wz::asset::AssetKey register_file_node(
            const wz::fs::Path& relative_path,
            wz::asset::SchemaID  schema,
            wz::asset::AssetType type
        );

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        wz::fs::Path            resource_root_;
    };

} // namespace wz::engine::assets
