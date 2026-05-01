#pragma once

namespace wz
{
    enum class Error
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
    struct Result
    {
        T value{};
        Error error = Error::None;

        explicit operator bool() const { return error == Error::None; }
    };
}