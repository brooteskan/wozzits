#include <file/filesystem.h>
#include <async/async.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <system_error>
#include <vector>

namespace
{ // Convert UTF-8 std::string ↔ UTF-16 (Win32 uses UTF-16)

    std::wstring utf8_to_wide(const std::string &str)
    {
        if (str.empty())
            return {};

        int size_needed = MultiByteToWideChar(
            CP_UTF8, 0,
            str.c_str(), (int)str.size(),
            nullptr, 0);

        std::wstring wstr(size_needed, 0);

        MultiByteToWideChar(
            CP_UTF8, 0,
            str.c_str(), (int)str.size(),
            wstr.data(), size_needed);

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

        std::string str(size_needed, 0);

        WideCharToMultiByte(
            CP_UTF8, 0,
            wstr.c_str(), (int)wstr.size(),
            str.data(), size_needed,
            nullptr, nullptr);

        return str;
    }

    static wz::FileError from_win32(DWORD err)
    {
        switch (err)
        {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return wz::FileError::NotFound;

        case ERROR_ACCESS_DENIED:
            return wz::FileError::PermissionDenied;

        case ERROR_ALREADY_EXISTS:
            return wz::FileError::AlreadyExists;

        case ERROR_INVALID_NAME:
            return wz::FileError::InvalidPath;

        default:
            return wz::FileError::IOError;
        }
    }
}

namespace wz::fs
{

    static FileError last_error()
    {
        return from_win32(GetLastError());
    }

    /// @brief Reads the contents of a file into a buffer.
    /// @param path The path to the file.
    /// @return The result containing the file contents or an error.
    wz::FileResult<wz::fs::Buffer>
    wz::fs::read_file(const Path &path)
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

        DWORD bytes_read = 0;
        if (!ReadFile(file, buffer.data(), (DWORD)buffer.size(), &bytes_read, nullptr))
        {
            result.error = last_error();
            CloseHandle(file);
            return result;
        }

        CloseHandle(file);

        buffer.resize(bytes_read);
        result.value = std::move(buffer);
        result.error = FileError::None;

        return result;
    }

    /// @brief Writes data to a file.
    /// @param path The path to the file.
    /// @param data The data to write.
    /// @param overwrite Whether to overwrite the file if it exists.
    /// @return The error code, or Error::None on success.
    wz::FileError
    wz::fs::write_file(const Path &path,
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

        DWORD written = 0;

        BOOL ok = WriteFile(
            file,
            data.data(),
            (DWORD)data.size(),
            &written,
            nullptr);

        CloseHandle(file);

        if (!ok)
            return last_error();

        return FileError::None;
    }

    /// @brief  Checks if a file or directory exists at the given path.
    /// @param path
    /// @return
    bool wz::fs::exists(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        DWORD attr = GetFileAttributesW(wpath.c_str());

        return attr != INVALID_FILE_ATTRIBUTES;
    }

    /// @brief Gets the size of a file at the given path.
    /// @param path The path to the file.
    /// @param out_error Optional pointer to store the error code.
    /// @return The size of the file, or 0 on error.
    std::uint64_t
    wz::fs::file_size(const Path &path, FileError *out_error)
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
        {
            if (out_error)
                *out_error = last_error();
            return 0;
        }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(file, &size))
        {
            if (out_error)
                *out_error = last_error();
            CloseHandle(file);
            return 0;
        }

        CloseHandle(file);

        if (out_error)
            *out_error = FileError::None;
        return static_cast<std::uint64_t>(size.QuadPart);
    }

    /// @brief Lists the contents of a directory.
    /// @param path The path to the directory.
    /// @return A result containing the list of entries or an error.
    wz::FileResult<std::vector<wz::fs::DirEntry>>
    wz::fs::list_directory(const Path &path)
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

    /// @brief Creates a directory and any necessary parent directories.
    /// @param path The path to the directory to create.
    /// @return An error code indicating the result of the operation.
    wz::FileError
    wz::fs::create_directories(const Path &path)
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

            return from_win32(err);
        }

        return FileError::None;
    }

    wz::FileError
    wz::fs::write_file_text(const Path &path,
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

    wz::FileError
    wz::fs::remove_file(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        BOOL ok = DeleteFileW(wpath.c_str());

        if (!ok)
        {
            return last_error();
        }

        return FileError::None;
    }

    wz::FileError
    wz::fs::remove_directory(const Path &path, bool recursive)
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

    void wz::fs::async_read_file(const Path &path,
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

        executor->post([path, callback]()
                       {
        auto result = read_file(path);
        callback(std::move(result)); });
    }

    void wz::fs::async_write_file(const Path &path,
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

        executor->post([path, data = std::move(data), callback, overwrite]()
                       {
        FileError err = write_file(path, data, overwrite);
        callback(err); });
    }

    Path normalize(const Path &path)
    {
        if (path.empty())
            return {};

        Path out;
        out.reserve(path.size());

        bool last_was_sep = false;

        for (char c : path)
        {
            if (c == '/' || c == '\\')
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

        // --- ROOT PRESERVATION FIX ---
        // If path is exactly "C:\" or "X:\", preserve it
        if (out.size() == 3 &&
            std::isalpha(out[0]) &&
            out[1] == ':' &&
            out[2] == '\\')
        {
            return out;
        }

        // Remove trailing separator for non-root paths
        if (out.size() > 1 && out.back() == '\\')
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

    Path parent_path(const Path &path)
    {
        if (path.empty())
            return {};

        // Find last separator (support both styles)
        size_t pos = path.find_last_of("\\/");

        if (pos == Path::npos)
            return {}; // no parent

        if (pos == 0)
        {
            // Handles cases like "\foo"
            return Path(1, path[0]);
        }

        // Special case: "C:\"
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

        size_t pos = path.find_last_of("\\/");

        if (pos == Path::npos)
            return path;

        if (pos + 1 >= path.size())
            return {};

        return path.substr(pos + 1);
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
        if (path.size() >= 2)
        {
            // Drive letter: C:\ or C:/
            if (std::isalpha(path[0]) && path[1] == ':')
                return true;
        }

        if (path.size() >= 2)
        {
            if ((path[0] == '\\' || path[0] == '/') &&
                (path[1] == '\\' || path[1] == '/'))
            {
                return true; // UNC path
            }
        }

        return false;
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

    wz::FileError set_current_directory(const Path &path)
    {
        std::wstring wpath = utf8_to_wide(path);

        BOOL ok = SetCurrentDirectoryW(wpath.c_str());

        if (!ok)
        {
            DWORD err = GetLastError();

            switch (err)
            {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                return FileError::NotFound;

            case ERROR_ACCESS_DENIED:
                return FileError::PermissionDenied;

            default:
                return FileError::IOError;
            }
        }

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

}