// window_engine/engine/assets/gaussian_splat/gaussian_splat_ply_importer.h
#pragma once

#include <span>
#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>
#include <file/filesystem.h>
#include <filesystem>
#include <cstdint>

namespace wz::engine::assets
{
    GaussianSplatImportResult import_gaussian_splat_ply_file(
        const std::filesystem::path& path);

    GaussianSplatImportResult import_gaussian_splat_ply_bytes(
        std::span<const std::uint8_t> bytes);

}