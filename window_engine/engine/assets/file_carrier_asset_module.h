#pragma once

// engine/assets/file_carrier_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>

#include <logging/logger.h>

namespace wz::engine::assets
{
    // Strip a single matched surrounding pair of ASCII double-quotes from an
    // authored path (Windows Explorer's "Copy as path" wraps the path). Only a
    // genuine leading+trailing pair is removed. This is the cleanup half of the
    // rooting convention, split out because carrier nodes STORE the authored
    // path and root it only when reading.
    [[nodiscard]] wz::fs::Path strip_file_carrier_path_quotes(
        const wz::fs::Path& path);

    // Resolve a file-carrier path against a resource root, mirroring how source
    // nodes are rooted: strip surrounding quotes, then return the path unchanged
    // when absolute, or joined onto `resource_root` when relative. This is the
    // single engine-side authority for the rooting convention so callers (the
    // editor ABI's GLB import, callers wanting a disk-ready path) never
    // reimplement it.
    //
    // NOTE: this is a READ-TIME operation. Do not use it to compute a path that
    // will be STORED on a carrier node — see register_file_node below.
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
    //
    // A registered node STORES the authored path (quotes stripped, otherwise
    // as given) in FileSourceDesc::full_path — it is NOT joined onto the
    // resource root. Rooting happens once, at read time, in every consumer:
    // the file/shader carrier compilers, key derivation, and wozzits_export
    // (which rewrites the stored path when relocating sources into a bundle).
    // Keeping the stored path relocatable is what lets an exported bundle and
    // a moved project resolve their own sources (#295).

    class FileCarrierAssetModule
    {
    public:
        // No Logger: this module has nothing to report. register_file_node's
        // only "failure" is a duplicate key, which is expected (several asset
        // nodes may share one file carrier), and resolve_path is total. It took
        // a Logger& for symmetry with its sibling modules and never read it,
        // which pinned a lifetime requirement on every caller for nothing.
        FileCarrierAssetModule(
            wz::asset::AssetSystem& system,
            wz::fs::Path            resource_root
        );

        // Register a file-backed source node in the shared AssetSystem.
        // path is canonicalised for the key; the node stores it as authored
        // (see the rooting note above) rather than joined onto the resource
        // root. Returns the node key, or a zero key on failure.
        [[nodiscard]] wz::asset::AssetKey register_file_node(
            const wz::fs::Path& path,
            wz::asset::SchemaID  schema,
            wz::asset::AssetType type
        );

        [[nodiscard]] wz::fs::Path resolve_path(
            const wz::fs::Path& path) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::fs::Path            resource_root_;
    };

} // namespace wz::engine::assets
