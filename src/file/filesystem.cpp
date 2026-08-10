#include <file/filesystem.h>
#include <async/async.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cctype>
#include <cstddef>
#include <system_error>
#include <vector>

namespace
{ // Convert UTF-8 std::string ↔ UTF-16 (Win32 uses UTF-16)

    // The conversions below check BOTH calls. The old code ignored the return of
    // the second call and sized the output by the first, so a mid-conversion
    // failure left a buffer that was the right LENGTH but partly zero-filled --
    // silently handing a corrupted path to the wide Win32 API. On any failure we
    // return an empty string instead; callers then hit a clean CreateFileW /
    // GetFileAttributesW failure (NotFound / InvalidPath) rather than operating
    // on a mangled path.
    std::wstring utf8_to_wide(const std::string &str)
    {
        if (str.empty())
            return {};

        int size_needed = MultiByteToWideChar(
            CP_UTF8, 0,
            str.c_str(), (int)str.size(),
            nullptr, 0);

        if (size_needed <= 0)
            return {};

        std::wstring wstr(size_needed, 0);

        int converted = MultiByteToWideChar(
            CP_UTF8, 0,
            str.c_str(), (int)str.size(),
            wstr.data(), size_needed);

        if (converted <= 0)
            return {};

        wstr.resize(converted);
        return wstr;
    }

    std::string wide_to_utf8(const std::wstring &wstr)
    {
        if (wstr.empty())
            return {};

        int size_needed = WideCharToMultiByte(
            CP_UTF8, 0,
            wstr.c_str(), (int)wstr.size(),
            nullptr, 0,
            nullptr, nullptr);

        if (size_needed <= 0)
            return {};

        std::string str(size_needed, 0);

        int converted = WideCharToMultiByte(
            CP_UTF8, 0,
            wstr.c_str(), (int)wstr.size(),
            str.data(), size_needed,
            nullptr, nullptr);

        if (converted <= 0)
            return {};

        str.resize(converted);
        return str;
    }

    static wz::fs::FileError from_win32(DWORD err)
    {
        switch (err)
        {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return wz::fs::FileError::NotFound;

        case ERROR_ACCESS_DENIED:
            return wz::fs::FileError::PermissionDenied;

        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return wz::fs::FileError::AlreadyExists;

        case ERROR_INVALID_NAME:
            return wz::fs::FileError::InvalidPath;

        default:
            return wz::fs::FileError::IOError;
        }
    }

    // Native Win32 diagnostics for the last failing call, kept per-thread so a
    // concurrent async op on another thread cannot clobber them (see the header
    // contract on last_os_error_code/message).
    thread_local DWORD g_last_os_code = 0;
    thread_local std::string g_last_os_message;

    std::string format_win32_message(DWORD err)
    {
        if (err == 0)
            return {};

        LPWSTR buffer = nullptr;
        DWORD len = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, err,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

        std::wstring wide;
        if (len && buffer)
            wide.assign(buffer, len);
        if (buffer)
            LocalFree(buffer);

        // FormatMessage appends ".\r\n"; trim it so the text reads cleanly when
        // concatenated into a caller's own message.
        while (!wide.empty() &&
               (wide.back() == L'\r' || wide.back() == L'\n' ||
                wide.back() == L' ' || wide.back() == L'.'))
            wide.pop_back();

        return wide_to_utf8(wide);
    }

    // Record the native error, then map it to the coarse FileError enum. Every
    // wz::fs failure path routes through here so last_os_error_* is populated.
    wz::fs::FileError capture_win32(DWORD err)
    {
        g_last_os_code = err;
        g_last_os_message = format_win32_message(err);
        return from_win32(err);
    }
}

namespace wz::fs
{

    static FileError last_error()
    {
        return capture_win32(GetLastError());
    }

    // ReadFile/WriteFile take a DWORD count, so a single call cannot express a
    // length >= 4 GiB -- it silently takes the length modulo 2^32. Both are also
    // permitted to transfer FEWER bytes than asked for at any size. Every whole-
    // file transfer below therefore loops in chunks and checks the TOTAL against
    // the size it set out to move; a mismatch is IOError, never a short buffer
    // handed back as if it were the whole file.
    //
    // 64 MiB keeps each individual I/O modest while costing only 64 iterations
    // on a 4 GiB file.
    static constexpr std::size_t kMaxIOChunkBytes = 64u * 1024u * 1024u;

    static DWORD io_chunk_size(std::size_t remaining)
    {
        return remaining > kMaxIOChunkBytes
                   ? static_cast<DWORD>(kMaxIOChunkBytes)
                   : static_cast<DWORD>(remaining);
    }

    /// @brief Reads the contents of a file into a buffer.
    /// @param path The path to the file.
    /// @return The result containing the file contents or an error.
    wz::fs::FileResult<wz::fs::Buffer>
    read_file(const Path &path)
    {
        FileResult<Buffer> result;

        std::wstring wpath = utf8_to_wide(path);

        HANDLE file = CreateFileW(
            wpath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE)
        {
            result.error = last_error();
            return result;
        }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(file, &size))
        {
            result.error = last_error();
            CloseHandle(file);
            return result;
        }

        Buffer buffer;
        buffer.resize(static_cast<size_t>(size.QuadPart));

        // See kMaxIOChunkBytes. Reading the whole file in ONE ReadFile call
        // truncated the count to 32 bits, so a 4 GiB file read as 0 bytes and a
        // 4 GiB + 1 KiB file read as 1 KiB -- and the old code then did
        // buffer.resize(bytes_read) and returned FileError::None, so a caller
        // could not tell a whole file from a prefix. That silence is the real
        // hazard: a decoder handed a prefix sees a well-formed-but-tiny asset
        // (a 32768-square Gaea .r32 becomes a 16x16 terrain), and anyone who
        // notices the ceiling is tempted to hand-roll their own reader instead.
        std::size_t total_read = 0;
        while (total_read < buffer.size())
        {
            DWORD bytes_read = 0;
            if (!ReadFile(file,
                          buffer.data() + total_read,
                          io_chunk_size(buffer.size() - total_read),
                          &bytes_read,
                          nullptr))
            {
                result.error = last_error();
                CloseHandle(file);
                return result;
            }

            // EOF before GetFileSizeEx's promise. CreateFileW above shares only
            // FILE_SHARE_READ, so a concurrent writer is already excluded --
            // this means the file genuinely changed underneath us.
            if (bytes_read == 0)
                break;

            total_read += bytes_read;
        }

        CloseHandle(file);

        if (total_read != buffer.size())
        {
            result.error = FileError::IOError;
            return result;
        }

        result.value = std::move(buffer);
        result.error = FileError::None;

        return result;
    }

    /// @brief Writes data to a file.
    /// @param path The path to the file.
    /// @param data The data to write.
    /// @param overwrite Whether to overwrite the file if it exists.
    /// @return The error code, or Error::None on success.
    wz::fs::FileError
    write_file(const Path &path,
                       const Buffer &data,
                       bool overwrite)
    {
        std::wstring wpath = utf8_to_wide(path);

        HANDLE file = CreateFileW(
            wpath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            overwrite ? CREATE_ALWAYS : CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return last_error();

        // Same defect as read_file, and worse in consequence: `written` was never
        // compared against data.size(), so a short write (disk full, quota) --
        // and any payload >= 4 GiB via the DWORD truncation -- produced a
        // TRUNCATED FILE reported as a successful write. This function is the
        // disk-cache write path and, through write_file_text, the scene and
        // scenelet JSON writer, so a silent partial write here is authored-data
        // loss.
        std::size_t total_written = 0;
        while (total_written < data.size())
        {
            DWORD written = 0;
            if (!WriteFile(file,
                           data.data() + total_written,
                           io_chunk_size(data.size() - total_written),
                           &written,
                           nullptr))
            {
                const FileError err = last_error();
                CloseHandle(file);
                return err;
            }

            // WriteFile reporting success having moved nothing would spin here.
            if (written == 0)
                break;

            total_written += written;
        }

        CloseHandle(file);

        if (total_written != data.size())
            return FileError::IOError;

        return FileError::None;
    }

    /// @brief  Checks if a file or directory exists at the given path.
    /// @param path
    /// @return
    bool exists(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        DWORD attr = GetFileAttributesW(wpath.c_str());

        return attr != INVALID_FILE_ATTRIBUTES;
    }

    bool is_directory(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        DWORD attr = GetFileAttributesW(wpath.c_str());

        return attr != INVALID_FILE_ATTRIBUTES &&
               (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool is_regular_file(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        DWORD attr = GetFileAttributesW(wpath.c_str());

        return attr != INVALID_FILE_ATTRIBUTES &&
               (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    FileResult<FileStatus> status(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        WIN32_FILE_ATTRIBUTE_DATA data;
        if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &data))
            return { FileStatus{}, last_error() };

        FileStatus s;
        s.exists = true;
        s.is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        s.is_regular_file = !s.is_directory;

        if (!s.is_directory)
        {
            LARGE_INTEGER size;
            size.LowPart = data.nFileSizeLow;
            size.HighPart = static_cast<LONG>(data.nFileSizeHigh);
            s.size = static_cast<std::uint64_t>(size.QuadPart);
        }

        return { s, FileError::None };
    }

    /// @brief Gets the size of a file at the given path.
    /// @param path The path to the file.
    /// @return A result containing the file size or an error.
    FileResult<std::uint64_t>
    file_size(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        HANDLE file = CreateFileW(
            wpath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return { 0, last_error() };

        LARGE_INTEGER size;
        if (!GetFileSizeEx(file, &size))
        {
            FileError err = last_error();
            CloseHandle(file);
            return { 0, err };
        }

        CloseHandle(file);
        return { static_cast<std::uint64_t>(size.QuadPart), FileError::None };
    }

    /// @brief Lists the contents of a directory.
    /// @param path The path to the directory.
    /// @return A result containing the list of entries or an error.
    wz::fs::FileResult<std::vector<wz::fs::DirEntry>>
    list_directory(const Path &path)
    {
        FileResult<std::vector<DirEntry>> result;

        std::wstring pattern = utf8_to_wide(path + "\\*");

        WIN32_FIND_DATAW data;
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &data);

        if (hFind == INVALID_HANDLE_VALUE)
        {
            result.error = last_error();
            return result;
        }

        std::vector<DirEntry> entries;

        do
        {
            std::wstring name = data.cFileName;

            if (name == L"." || name == L"..")
                continue;

            DirEntry entry;
            entry.name = wide_to_utf8(name);
            entry.is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            LARGE_INTEGER size;
            size.LowPart = data.nFileSizeLow;
            size.HighPart = data.nFileSizeHigh;

            entry.size = static_cast<std::uint64_t>(size.QuadPart);

            entries.push_back(std::move(entry));

        } while (FindNextFileW(hFind, &data));

        FindClose(hFind);

        result.value = std::move(entries);
        result.error = FileError::None;

        return result;
    }

    FileError copy_file(const Path &from, const Path &to, CopyOptions options)
    {
        // SkipExisting is a pre-check: an existing destination is left untouched
        // and reported as success.
        if (options == CopyOptions::SkipExisting && exists(to))
            return FileError::None;

        std::wstring wfrom = utf8_to_wide(from);
        std::wstring wto = utf8_to_wide(to);

        // bFailIfExists TRUE => CopyFileW fails (ERROR_FILE_EXISTS) if `to`
        // exists; FALSE => it overwrites.
        const BOOL fail_if_exists =
            (options == CopyOptions::FailIfExists) ? TRUE : FALSE;

        if (!CopyFileW(wfrom.c_str(), wto.c_str(), fail_if_exists))
            return last_error();

        return FileError::None;
    }

    FileError copy_tree(const Path &from, const Path &to, CopyOptions options)
    {
        const FileResult<FileStatus> st = status(from);
        if (!st)
            return st.error;

        // A plain file source is just a file copy.
        if (!st.value.is_directory)
            return copy_file(from, to, options);

        // Ensure the destination directory exists (idempotent). CopyOptions
        // governs file collisions, not directory reuse -- a tree cannot be
        // reproduced without materializing its directories.
        FileError err = create_directories(to);
        if (err != FileError::None)
            return err;

        const FileResult<std::vector<DirEntry>> listing = list_directory(from);
        if (!listing)
            return listing.error;

        for (const DirEntry &entry : listing.value)
        {
            const Path child_from = join(from, entry.name);
            const Path child_to = join(to, entry.name);

            err = entry.is_directory
                      ? copy_tree(child_from, child_to, options)
                      : copy_file(child_from, child_to, options);

            if (err != FileError::None)
                return err;
        }

        return FileError::None;
    }

    FileError rename(const Path &from, const Path &to)
    {
        std::wstring wfrom = utf8_to_wide(from);
        std::wstring wto = utf8_to_wide(to);

        // REPLACE_EXISTING overwrites an existing file destination;
        // COPY_ALLOWED lets a cross-volume file move fall back to copy+delete.
        if (!MoveFileExW(wfrom.c_str(), wto.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
            return last_error();

        return FileError::None;
    }

    static FileError walk_recursive(const Path &dir, const WalkVisitor &visitor)
    {
        std::wstring pattern = utf8_to_wide(join(dir, "*"));

        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileW(pattern.c_str(), &data);

        // An inaccessible/unreadable directory is surfaced, not swallowed.
        if (find == INVALID_HANDLE_VALUE)
            return last_error();

        FileError result = FileError::None;

        do
        {
            const std::wstring wname = data.cFileName;
            if (wname == L"." || wname == L"..")
                continue;

            DirEntry entry;
            entry.name = wide_to_utf8(wname);
            entry.is_directory =
                (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            LARGE_INTEGER size;
            size.LowPart = data.nFileSizeLow;
            size.HighPart = static_cast<LONG>(data.nFileSizeHigh);
            entry.size = entry.is_directory
                             ? 0
                             : static_cast<std::uint64_t>(size.QuadPart);

            const Path full = join(dir, entry.name);

            // Pre-order: the entry is visited before any of its children.
            visitor(full, entry);

            // Descend into real subdirectories only. A directory reparse point
            // (symlink/junction) is visited but NOT followed, so the walk cannot
            // cycle or escape the tree.
            const bool is_reparse =
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

            if (entry.is_directory && !is_reparse)
            {
                const FileError sub = walk_recursive(full, visitor);
                // Keep the first error but keep walking siblings, so a single
                // unreadable subtree does not hide the rest of the tree.
                if (sub != FileError::None && result == FileError::None)
                    result = sub;
            }

        } while (FindNextFileW(find, &data));

        FindClose(find);
        return result;
    }

    FileError walk(const Path &root, const WalkVisitor &visitor)
    {
        if (!visitor)
            return FileError::InvalidArgument;

        return walk_recursive(root, visitor);
    }

    /// @brief Creates a directory and any necessary parent directories.
    /// @param path The path to the directory to create.
    /// @return An error code indicating the result of the operation.
    wz::fs::FileError
    create_directories(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        std::wstring current;

        for (size_t i = 0; i < wpath.size(); ++i)
        {
            current += wpath[i];

            if (wpath[i] == L'\\' || wpath[i] == L'/')
            {
                CreateDirectoryW(current.c_str(), nullptr);
            }
        }

        BOOL ok = CreateDirectoryW(wpath.c_str(), nullptr);

        if (!ok)
        {
            DWORD err = GetLastError();
            if (err == ERROR_ALREADY_EXISTS)
                return FileError::None;

            return capture_win32(err);
        }

        return FileError::None;
    }

    FileError create_parent_directories(const Path &path)
    {
        const Path parent = parent_path(path);

        // No parent component (a bare filename, or a root): nothing to create.
        if (parent.empty())
            return FileError::None;

        return create_directories(parent);
    }

    wz::fs::FileError
    write_file_text(const Path &path,
                            const std::string &text,
                            bool overwrite)
    {
        Buffer buffer;
        buffer.reserve(text.size());

        buffer.insert(buffer.end(),
                      reinterpret_cast<const Byte *>(text.data()),
                      reinterpret_cast<const Byte *>(text.data() + text.size()));

        return write_file(path, buffer, overwrite);
    }

    wz::fs::FileResult<std::string>
    read_file_text(const Path &path)
    {
        const FileResult<Buffer> raw = read_file(path);

        FileResult<std::string> result;
        result.error = raw.error;
        if (raw)
        {
            result.value.assign(
                reinterpret_cast<const char *>(raw.value.data()),
                raw.value.size());
        }
        return result;
    }

    wz::fs::FileError
    remove_file(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        BOOL ok = DeleteFileW(wpath.c_str());

        if (!ok)
        {
            return last_error();
        }

        return FileError::None;
    }

    wz::fs::FileError
    remove_directory(const Path &path, bool recursive)
    {
        std::wstring wpath = utf8_to_wide(path);

        if (!recursive)
        {
            BOOL ok = RemoveDirectoryW(wpath.c_str());

            if (!ok)
            {
                return last_error();
            }

            return FileError::None;
        }

        // Recursive deletion
        WIN32_FIND_DATAW data;

        std::wstring search_path = wpath + L"\\*";
        HANDLE hFind = FindFirstFileW(search_path.c_str(), &data);

        if (hFind == INVALID_HANDLE_VALUE)
        {
            return last_error();
        }

        do
        {
            std::wstring name = data.cFileName;

            if (name == L"." || name == L"..")
                continue;

            std::wstring full_path = wpath + L"\\" + name;

            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                FileError err = remove_directory(wide_to_utf8(full_path), true);
                if (err != FileError::None)
                {
                    FindClose(hFind);
                    return err;
                }
            }
            else
            {
                if (!DeleteFileW(full_path.c_str()))
                {
                    FindClose(hFind);
                    return last_error();
                }
            }

        } while (FindNextFileW(hFind, &data));

        FindClose(hFind);

        if (!RemoveDirectoryW(wpath.c_str()))
        {
            return last_error();
        }

        return FileError::None;
    }

    void async_read_file(const Path &path,
                                 ReadCallback callback)
    {
        auto *executor = wz::get_async_executor();

        if (!executor)
        {
            FileResult<Buffer> result;
            result.error = FileError::IOError;
            callback(std::move(result));
            return;
        }

        if (!executor->post([path, callback]()
                            {
        auto result = read_file(path);
        callback(std::move(result)); }))
        {
            // The executor refused the job (shutting down), so nothing will run
            // the callback. Answer here instead, exactly as the no-executor path
            // above does -- a caller waiting on this callback must never be left
            // waiting forever.
            FileResult<Buffer> result;
            result.error = FileError::IOError;
            callback(std::move(result));
        }
    }

    void async_write_file(const Path &path,
                                  Buffer data,
                                  WriteCallback callback,
                                  bool overwrite)
    {
        auto *executor = wz::get_async_executor();

        if (!executor)
        {
            callback(FileError::IOError);
            return;
        }

        if (!executor->post([path, data = std::move(data), callback, overwrite]()
                            {
        FileError err = write_file(path, data, overwrite);
        callback(err); }))
        {
            // Refused (shutting down) -- see async_read_file.
            callback(FileError::IOError);
        }
    }

    static bool is_separator(char c)
    {
        return c == '/' || c == '\\';
    }

    Path normalize(const Path &path)
    {
        if (path.empty())
            return {};

        Path out;
        out.reserve(path.size());

        // Preserve a leading UNC double-separator: "\\server\share" (or the
        // forward-slash spelling) must survive as "\\...". The generic run-
        // collapse below folds every run of separators to a single '\\', which
        // would turn "\\server" into "\server" and destroy the UNC root -- so
        // emit both leading separators verbatim and start collapsing after them.
        std::size_t start = 0;
        bool unc = false;
        if (path.size() >= 2 && is_separator(path[0]) && is_separator(path[1]))
        {
            unc = true;
            out.push_back('\\');
            out.push_back('\\');
            start = 2;
            // Fold any extra leading separators (\\\\server -> \\server).
            while (start < path.size() && is_separator(path[start]))
                ++start;
        }

        bool last_was_sep = false;
        for (std::size_t i = start; i < path.size(); ++i)
        {
            const char c = path[i];
            if (is_separator(c))
            {
                if (!last_was_sep)
                {
                    out.push_back('\\');
                    last_was_sep = true;
                }
            }
            else
            {
                out.push_back(c);
                last_was_sep = false;
            }
        }

        // Preserve a bare drive root exactly as "C:\".
        if (out.size() == 3 &&
            std::isalpha(static_cast<unsigned char>(out[0])) &&
            out[1] == ':' &&
            out[2] == '\\')
        {
            return out;
        }

        // Drop a trailing separator on non-root paths. For a UNC path keep the
        // leading "\\" intact and only strip separators beyond it.
        const std::size_t min_len = unc ? 2 : 1;
        if (out.size() > min_len && out.back() == '\\')
        {
            out.pop_back();
        }

        return out;
    }

    Path join(const Path &a, const Path &b)
    {
        if (a.empty())
            return b;

        if (b.empty())
            return a;

        bool a_ends_sep = (a.back() == '\\' || a.back() == '/');
        bool b_starts_sep = (b.front() == '\\' || b.front() == '/');

        if (a_ends_sep && b_starts_sep)
        {
            return a + b.substr(1);
        }
        else if (!a_ends_sep && !b_starts_sep)
        {
            return a + "\\" + b;
        }
        else
        {
            return a + b;
        }
    }

    // Length of `path` with trailing separators ignored, but never below 1 so a
    // rooted path like "\" or "/" is not emptied. Used by parent_path/filename
    // so "a/b/" splits the same as "a/b".
    static std::size_t length_without_trailing_seps(const Path &path)
    {
        std::size_t end = path.size();
        while (end > 1 && is_separator(path[end - 1]))
            --end;
        return end;
    }

    Path parent_path(const Path &path)
    {
        if (path.empty())
            return {};

        // Ignore trailing separators so parent_path("a/b/") is "a", matching
        // filename("a/b/") == "b".
        const std::size_t end = length_without_trailing_seps(path);

        size_t pos = path.find_last_of("\\/", end - 1);

        if (pos == Path::npos)
            return {}; // no parent

        if (pos == 0)
        {
            // Handles cases like "\foo"
            return Path(1, path[0]);
        }

        // Special case: "C:\foo" -> keep the drive-root separator ("C:\").
        if (pos == 2 && path.size() >= 3 && path[1] == ':')
        {
            return path.substr(0, 3);
        }

        return path.substr(0, pos);
    }

    Path filename(const Path &path)
    {
        if (path.empty())
            return {};

        // Ignore trailing separators: filename("a/b/") is "b", the entry the
        // path names, not an empty leaf.
        const std::size_t end = length_without_trailing_seps(path);

        // The path was nothing but separators ("/", "\\\\").
        if (end == 1 && is_separator(path[0]))
            return {};

        size_t pos = path.find_last_of("\\/", end - 1);

        const std::size_t begin = (pos == Path::npos) ? 0 : pos + 1;
        if (begin >= end)
            return {};

        return path.substr(begin, end - begin);
    }

    Path stem(const Path &path)
    {
        Path file = filename(path);

        if (file.empty())
            return {};

        // ignore leading dot files like ".gitignore"
        size_t pos = file.find_last_of('.');

        if (pos == Path::npos || pos == 0)
            return file;

        return file.substr(0, pos);
    }

    Path extension(const Path &path)
    {
        Path file = filename(path);

        if (file.empty())
            return {};

        size_t pos = file.find_last_of('.');

        if (pos == Path::npos || pos == 0 || pos + 1 >= file.size())
            return {};

        return file.substr(pos + 1);
    }

    bool is_absolute(const Path &path)
    {
        // UNC path: two leading separators, e.g. "\\server\share".
        if (path.size() >= 2 && is_separator(path[0]) && is_separator(path[1]))
            return true;

        // Drive-absolute: "C:\" or "C:/". A drive letter and colon NOT followed
        // by a separator ("C:foo") is drive-RELATIVE -- it resolves against the
        // per-drive current directory, so it is not a fully-qualified path.
        if (path.size() >= 3 &&
            std::isalpha(static_cast<unsigned char>(path[0])) &&
            path[1] == ':' &&
            is_separator(path[2]))
            return true;

        // A single leading separator ("\foo") is root-relative to the current
        // drive, not absolute -- matching std::filesystem on Windows.
        return false;
    }

    // ASCII case-insensitive equality. Windows path comparison is case-
    // insensitive; folding only ASCII is the pragmatic choice -- non-ASCII UTF-8
    // bytes compare exactly, which is correct for identical encodings.
    static bool path_iequals(const Path &a, const Path &b)
    {
        if (a.size() != b.size())
            return false;

        for (std::size_t i = 0; i < a.size(); ++i)
        {
            char ca = a[i];
            char cb = b[i];
            if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
            if (ca != cb)
                return false;
        }
        return true;
    }

    // Split a normalized, absolute path into its root ("C:" or "\\server\share")
    // and the components after it. Returns false if `p` is not recognisably
    // absolute (no drive or UNC root).
    static bool split_absolute(const Path &p, Path &root, std::vector<Path> &parts)
    {
        auto push_components = [&](std::size_t i)
        {
            while (i < p.size())
            {
                while (i < p.size() && is_separator(p[i])) ++i;
                std::size_t begin = i;
                while (i < p.size() && !is_separator(p[i])) ++i;
                if (i > begin)
                    parts.push_back(p.substr(begin, i - begin));
            }
        };

        // UNC: "\\server\share"
        if (p.size() >= 2 && is_separator(p[0]) && is_separator(p[1]))
        {
            std::size_t i = 2;
            while (i < p.size() && !is_separator(p[i])) ++i;   // server
            if (i < p.size()) ++i;                             // separator
            while (i < p.size() && !is_separator(p[i])) ++i;   // share
            root = p.substr(0, i);
            push_components(i);
            return true;
        }

        // Drive: "C:"
        if (p.size() >= 2 &&
            std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':')
        {
            root = p.substr(0, 2);
            push_components(2);
            return true;
        }

        return false;
    }

    bool has_extension(const Path &path, const Path &ext)
    {
        Path want = ext;
        if (!want.empty() && want.front() == '.')
            want.erase(want.begin());

        return path_iequals(extension(path), want);
    }

    Path replace_extension(const Path &path, const Path &ext)
    {
        if (path.empty())
            return {};

        // Locate the filename span (ignoring trailing separators), then the
        // extension dot within it. A dot at the start of the filename is a
        // dotfile, not an extension boundary.
        const std::size_t end = length_without_trailing_seps(path);
        const std::size_t sep = path.find_last_of("\\/", end - 1);
        const std::size_t name_begin = (sep == Path::npos) ? 0 : sep + 1;

        const std::size_t dot = path.rfind('.', end - 1);
        const bool has_ext = (dot != Path::npos && dot > name_begin && dot < end);
        const std::size_t cut = has_ext ? dot : end;

        Path result = path.substr(0, cut);
        if (!ext.empty())
        {
            result += '.';
            result += (ext.front() == '.') ? ext.substr(1) : ext;
        }
        return result;
    }

    FileResult<Path> absolute(const Path &path)
    {
        if (path.empty())
            return { Path{}, FileError::InvalidArgument };

        if (is_absolute(path))
            return { normalize(path), FileError::None };

        const Path cwd = current_directory();
        if (cwd.empty())
            return { Path{}, FileError::IOError };

        return { normalize(join(cwd, path)), FileError::None };
    }

    FileResult<Path> weakly_canonical(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        DWORD needed = GetFullPathNameW(wpath.c_str(), 0, nullptr, nullptr);
        if (needed == 0)
            return { Path{}, last_error() };

        std::wstring buffer(needed, L'\0');
        DWORD written = GetFullPathNameW(wpath.c_str(), needed, buffer.data(), nullptr);
        if (written == 0 || written >= needed)
            return { Path{}, last_error() };

        buffer.resize(written);
        return { normalize(wide_to_utf8(buffer)), FileError::None };
    }

    FileResult<Path> canonical(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        // Zero access + FILE_FLAG_BACKUP_SEMANTICS lets this open a handle to a
        // directory as well as a file, without needing read permission.
        HANDLE handle = CreateFileW(
            wpath.c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);

        if (handle == INVALID_HANDLE_VALUE)
            return { Path{}, last_error() };

        const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;

        DWORD needed = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
        if (needed == 0)
        {
            FileError err = last_error();
            CloseHandle(handle);
            return { Path{}, err };
        }

        std::wstring buffer(needed, L'\0');
        DWORD written = GetFinalPathNameByHandleW(handle, buffer.data(), needed, flags);
        CloseHandle(handle);

        if (written == 0 || written >= needed)
            return { Path{}, FileError::IOError };

        buffer.resize(written);
        Path result = wide_to_utf8(buffer);

        // GetFinalPathNameByHandleW returns an extended-length path. Strip the
        // "\\?\" prefix, and turn "\\?\UNC\server\share" back into the UNC form
        // "\\server\share".
        if (result.rfind("\\\\?\\UNC\\", 0) == 0)
            result = "\\\\" + result.substr(8);
        else if (result.rfind("\\\\?\\", 0) == 0)
            result = result.substr(4);

        return { result, FileError::None };
    }

    FileResult<Path> relative(const Path &path, const Path &base)
    {
        FileResult<Path> abs_path = absolute(path);
        if (!abs_path)
            return { Path{}, abs_path.error };

        FileResult<Path> abs_base = absolute(base);
        if (!abs_base)
            return { Path{}, abs_base.error };

        Path proot, broot;
        std::vector<Path> pparts, bparts;
        if (!split_absolute(abs_path.value, proot, pparts) ||
            !split_absolute(abs_base.value, broot, bparts))
            return { Path{}, FileError::InvalidArgument };

        // Different roots (e.g. different drives) have no relative path.
        if (!path_iequals(proot, broot))
            return { Path{}, FileError::None };

        std::size_t common = 0;
        while (common < pparts.size() && common < bparts.size() &&
               path_iequals(pparts[common], bparts[common]))
            ++common;

        std::vector<Path> out;
        for (std::size_t i = common; i < bparts.size(); ++i)
            out.push_back("..");
        for (std::size_t i = common; i < pparts.size(); ++i)
            out.push_back(pparts[i]);

        if (out.empty())
            return { Path("."), FileError::None };

        Path result = out.front();
        for (std::size_t i = 1; i < out.size(); ++i)
        {
            result += '\\';
            result += out[i];
        }
        return { result, FileError::None };
    }

    const char *to_string(FileError error)
    {
        switch (error)
        {
        case FileError::None:             return "no error";
        case FileError::InvalidArgument:  return "invalid argument";
        case FileError::NotFound:         return "not found";
        case FileError::PermissionDenied: return "permission denied";
        case FileError::IOError:          return "I/O error";
        case FileError::InvalidPath:      return "invalid path";
        case FileError::AlreadyExists:    return "already exists";
        case FileError::Unknown:          return "unknown error";
        }
        return "unknown error";
    }

    std::uint32_t last_os_error_code()
    {
        return static_cast<std::uint32_t>(g_last_os_code);
    }

    std::string last_os_error_message()
    {
        return g_last_os_message;
    }

    Path current_directory()
    {
        DWORD size = GetCurrentDirectoryW(0, nullptr);
        if (size == 0)
            return {};

        std::wstring buffer(size, L'\0');

        DWORD result = GetCurrentDirectoryW(size, buffer.data());
        if (result == 0)
            return {};

        buffer.resize(result);

        return wide_to_utf8(buffer);
    }

    wz::fs::FileError set_current_directory(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        BOOL ok = SetCurrentDirectoryW(wpath.c_str());

        if (!ok)
            return capture_win32(GetLastError());

        return FileError::None;
    }

    Path executable_path()
    {
        std::wstring buffer(1024, L'\0');

        DWORD size = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            (DWORD)buffer.size());

        if (size == 0)
            return {};

        while (size == buffer.size())
        {
            buffer.resize(buffer.size() * 2);

            size = GetModuleFileNameW(
                nullptr,
                buffer.data(),
                (DWORD)buffer.size());

            if (size == 0)
                return {};
        }

        buffer.resize(size);

        return wide_to_utf8(buffer);
    }

    Path temp_directory_path()
    {
        std::wstring buffer(MAX_PATH, L'\0');

        DWORD size = GetTempPathW((DWORD)buffer.size(), buffer.data());

        if (size == 0)
            return {};

        buffer.resize(size);

        return wide_to_utf8(buffer);
    }

}