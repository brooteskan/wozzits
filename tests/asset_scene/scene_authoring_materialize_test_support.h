#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

// tests/asset_scene/scene_authoring_materialize.cpp

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scene/scene_authoring_materialize.h>

#include <external/tinyexr/tinyexr.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    bool write_raw_f32(
        const wz::fs::Path& path,
        const std::vector<float>& values)
    {
        const size_t byte_count = values.size() * sizeof(float);
        wz::fs::Buffer bytes(byte_count);
        std::memcpy(bytes.data(), values.data(), byte_count);
        return wz::fs::write_file(path, bytes, true) == wz::fs::FileError::None;
    }

    wz::fs::Buffer make_test_exr_bytes(const std::vector<float>& rgba)
    {
        unsigned char* data = nullptr;
        const char* error = nullptr;
        const int size = SaveEXRToMemory(
            rgba.data(),
            2,
            2,
            4,
            0,
            &data,
            &error);
        if (error) {
            FreeEXRErrorMessage(error);
        }
        wz::fs::Buffer out;
        if (size > 0 && data) {
            out.assign(data, data + size);
        }
        std::free(data);
        return out;
    }
}























#if defined(__clang__)
#pragma clang diagnostic pop
#endif
