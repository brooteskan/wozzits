#pragma once

namespace wz::fs
{
    enum class FileError
    {
        None,
        InvalidArgument,
        NotFound,
        PermissionDenied,
        IOError,
        InvalidPath,
        AlreadyExists,
        Unknown
    };

    template <typename T>
    struct FileResult
    {
        T value{};
        FileError error = FileError::None;

        operator bool() const { return error == FileError::None; }
    };
}