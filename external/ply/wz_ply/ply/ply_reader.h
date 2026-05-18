// external/ply/include/wz_external_ply/ply_reader.h
#pragma once

#include "ply_document.h"

#include <filesystem>
#include <span>

namespace wz::external::ply
{
    ReadResult read_ply_file(const std::filesystem::path& path);

    ReadResult read_ply_bytes(std::span<const std::uint8_t> bytes);
}