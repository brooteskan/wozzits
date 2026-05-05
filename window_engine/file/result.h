#pragma once

namespace wz
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

        explicit operator bool() const { return error == FileError::None; }
    };
}