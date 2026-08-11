#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <file/result.h>

namespace wz::fs
{

    using Path = std::string;
    using Byte = std::uint8_t;
    using Buffer = std::vector<Byte>;

    struct DirEntry
    {
        std::string name;
        bool is_directory = false;
        std::uint64_t size = 0;
    };

    struct FileStatus
    {
        bool exists = false;
        bool is_directory = false;
        // A regular file is anything that exists and is not a directory. Reparse
        // points (symlinks/junctions) to a file report as regular; this is a
        // deliberately pragmatic definition, narrower than std::filesystem's.
        bool is_regular_file = false;
        std::uint64_t size = 0;   // 0 for directories
    };

    // How copy_file / copy_tree treat a destination FILE that already exists.
    enum class CopyOptions
    {
        FailIfExists,       // return AlreadyExists (the default)
        OverwriteExisting,  // replace it
        SkipExisting,       // leave it as-is and report success
    };

    //
    FileResult<Buffer> read_file(const Path &path);

    //
    FileResult<std::string> read_file_text(const Path &path);

    //
    FileError write_file(const Path &path,
                     const Buffer &data,
                     bool overwrite = true);

    //
    FileError write_file_text(const Path &path,
                          const std::string &text,
                          bool overwrite = true);

    // ─── Crash-atomic replace ───────────────────────────────────────────────
    // Replace `path`'s contents such that a reader (or a crash) NEVER observes a
    // partial file: the bytes are staged in a sibling temp, flushed to stable
    // storage, then published with a single MoveFileEx replace. Either the old
    // contents or the new ones are on disk -- never a splice of the two.
    //
    // Use this for AUTHORED data whose loss is unrecoverable. Plain write_file
    // opens the destination CREATE_ALWAYS, which truncates it up front, so a
    // process death anywhere in the write leaves a half-file that the next load
    // cannot parse. For a scene document that is authored-data loss: the reader
    // treats an unparseable file as "no scene yet".
    //
    // Costs, all of which plain write_file avoids: a second file briefly exists
    // beside the destination (and survives a crash as visible litter), the write
    // is flushed rather than left to the cache, and the destination's identity
    // changes -- an open handle, hardlink, or ACL on the OLD file does not follow
    // the replace. Not for high-frequency or cache-like writes.
    //
    // Always overwrites; there is no create-only mode (that is write_file's
    // `overwrite = false`). Does NOT create parent directories.
    FileError write_file_atomic(const Path &path, const Buffer &data);

    // write_file_atomic for text, matching write_file_text's encoding (raw UTF-8
    // bytes, no BOM).
    FileError write_file_text_atomic(const Path &path, const std::string &text);

    //
    bool exists(const Path &path);

    // True only if the path exists and is a directory.
    bool is_directory(const Path &path);

    // True if the path exists and is not a directory (see FileStatus).
    bool is_regular_file(const Path &path);

    // Existence, kind, and size in one query. On a missing path the result
    // carries FileError::NotFound and a default-constructed FileStatus.
    FileResult<FileStatus> status(const Path &path);

    //
    FileResult<std::uint64_t> file_size(const Path &path);

    //
    FileError create_directories(const Path &path);

    // Creates the parent directory of `path` (and any missing ancestors). A no-op
    // returning None when `path` has no parent component. The companion the
    // copy/rename sites need before writing a destination.
    FileError create_parent_directories(const Path &path);

    //
    FileError remove_file(const Path &path);

    //
    FileError remove_directory(const Path &path, bool recursive = false);

    //
    FileResult<std::vector<DirEntry>> list_directory(const Path &path);

    // Copies a single file. Does NOT create the destination's parent directory
    // (use create_parent_directories first); see CopyOptions for the
    // already-exists policy.
    FileError copy_file(const Path &from,
                        const Path &to,
                        CopyOptions options = CopyOptions::FailIfExists);

    // Recursively copies a directory tree (or delegates to copy_file when `from`
    // is a plain file). Destination directories are created as needed;
    // CopyOptions governs collisions on the FILES within.
    FileError copy_tree(const Path &from,
                        const Path &to,
                        CopyOptions options = CopyOptions::FailIfExists);

    // Moves/renames a file or directory, replacing an existing destination.
    FileError rename(const Path &from, const Path &to);

    // Called once per entry discovered under a walk root, pre-order. `full_path`
    // is the entry's path joined onto the walk root; `entry` carries its name,
    // kind, and size (size 0 for directories) -- the same data list_directory
    // reports, so a walk needs no extra per-entry stat.
    using WalkVisitor = std::function<void(const Path &full_path,
                                           const DirEntry &entry)>;

    // Recursively visits every entry UNDER `root` (not `root` itself), pre-order.
    // Policy: does NOT descend into directory reparse points (symlinks/junctions),
    // so it cannot cycle or escape the tree. An unreadable subdirectory is
    // surfaced as the returned FileError (the first one encountered) rather than
    // silently skipped; sibling entries are still visited. Returns None on a
    // fully-successful walk.
    FileError walk(const Path &root, const WalkVisitor &visitor);

    //
    Path normalize(const Path &path);

    //
    Path join(const Path &a, const Path &b);

    //
    Path parent_path(const Path &path);

    //
    Path filename(const Path &path);

    //
    Path stem(const Path &path);

    //
    Path extension(const Path &path);

    //
    bool is_absolute(const Path &path);

    // Note: extension()/stem() treat the suffix WITHOUT a leading dot ("json",
    // not ".json"). The helpers below follow that convention.

    // True if path's extension equals `ext`, compared ASCII-case-insensitively.
    // `ext` may be given with or without a leading dot: has_extension(p, "json")
    // and has_extension(p, ".json") are equivalent.
    bool has_extension(const Path &path, const Path &ext);

    // Returns `path` with its extension replaced by `ext` (a leading dot in
    // `ext` is optional). An empty `ext` removes the extension. Dotfiles like
    // ".gitignore" have no extension, so the new one is appended after them.
    Path replace_extension(const Path &path, const Path &ext);

    // Lexically resolves `path` to an absolute path (joining with the current
    // directory when relative), then normalizes. Does not touch the disk and
    // does not resolve "."/".." beyond what normalize does.
    FileResult<Path> absolute(const Path &path);

    // Full path with "."/".." collapsed and the current directory applied
    // (GetFullPathNameW). Does NOT require the path to exist.
    FileResult<Path> weakly_canonical(const Path &path);

    // Real on-disk path with symlinks/junctions resolved and the "\\?\" prefix
    // stripped (GetFinalPathNameByHandleW). Requires the path to EXIST.
    FileResult<Path> canonical(const Path &path);

    // `path` expressed relative to `base` (both resolved to absolute first),
    // using ".." to ascend as needed. Returns "." when they are equal, and an
    // empty path (with FileError::None) when they share no common root, e.g.
    // different drives -- matching std::filesystem::relative.
    FileResult<Path> relative(const Path &path, const Path &base);

    // Human-readable, stateless description of a FileError enumerator.
    const char *to_string(FileError error);

    // Native Win32 diagnostics for the MOST RECENT failing wz::fs call ON THE
    // CALLING THREAD. Only meaningful right after a call returned a non-None
    // FileError; a successful call leaves the previous value in place. For the
    // async_* helpers the operation and its callback run on the same executor
    // thread, so these are valid when read from within the callback.
    std::uint32_t last_os_error_code();
    std::string last_os_error_message();

    using ReadCallback = std::function<void(FileResult<Buffer>)>;
    using WriteCallback = std::function<void(FileError)>;

    // Read/write off the calling thread, via the installed wz::IAsyncExecutor
    // (async/async.h) -- in the engine, the IO lane.
    //
    // THE CALLBACK ALWAYS RUNS, EXACTLY ONCE. It is the only channel these verbs
    // have, so every failure is delivered through it rather than thrown: an I/O
    // error, an allocation failure inside the operation (a whole-file read buffer
    // is the obvious one), no executor installed, and an executor that refused
    // the job because it is shutting down all arrive as a FileError.
    //
    // WHICH THREAD it runs on depends on how far the work got. Normally: the
    // executor thread, which is what makes last_os_error_code() above meaningful
    // inside it. But the two paths that never reach the executor -- none
    // installed, or the post refused -- answer with FileError::IOError
    // SYNCHRONOUSLY, on the caller's thread, before the call returns. A callback
    // that takes a lock the caller already holds, or that assumes it is off the
    // frame thread, must tolerate that.
    void async_read_file(const Path &path,
                         ReadCallback callback);

    // See async_read_file for the callback contract. `data` is copied into the
    // job, so the caller's buffer is free the moment this returns.
    void async_write_file(const Path &path,
                          Buffer data,
                          WriteCallback callback,
                          bool overwrite = true);

    Path current_directory();

    FileError set_current_directory(const Path &path);

    Path executable_path();

    Path temp_directory_path();
}