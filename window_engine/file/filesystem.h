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

    //
    FileResult<Buffer> read_file(const Path &path);

    //
    FileError write_file(const Path &path,
                     const Buffer &data,
                     bool overwrite = true);

    //
    FileError write_file_text(const Path &path,
                          const std::string &text,
                          bool overwrite = true);

    //
    bool exists(const Path &path);

    //
    std::uint64_t file_size(const Path &path, FileError *out_error = nullptr);

    //
    FileError create_directories(const Path &path);

    //
    FileError remove_file(const Path &path);

    //
    FileError remove_directory(const Path &path, bool recursive = false);

    //
    FileResult<std::vector<DirEntry>> list_directory(const Path &path);

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

    using ReadCallback = std::function<void(FileResult<Buffer>)>;
    using WriteCallback = std::function<void(FileError)>;

    //
    void async_read_file(const Path &path,
                         ReadCallback callback);

    //
    void async_write_file(const Path &path,
                          Buffer data,
                          WriteCallback callback,
                          bool overwrite = true);

    Path current_directory();

    FileError set_current_directory(const Path &path);

    Path executable_path();
}