// window_engine/engine/assets/gaussian_splat/gaussian_splat_ply_importer.h
#pragma once

#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>

#include <filesystem>

namespace wz::engine::assets
{
    GaussianSplatImportResult import_gaussian_splat_ply_file(
        const std::filesystem::path& path);
}