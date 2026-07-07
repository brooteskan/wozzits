#include <engine/assets/hdri/hdri_image_loader.h>

#include <file/filesystem.h>

#include <tinyexr.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        struct TinyEXRError
        {
            const char* message = nullptr;

            ~TinyEXRError()
            {
                if (message) {
                    FreeEXRErrorMessage(message);
                }
            }
        };

        struct HDRImageFileCache
        {
            std::mutex mutex;
            std::unordered_map<std::string, std::shared_ptr<const HDRImageData>>
                images;
            std::vector<std::string> order;
        };

        HDRImageFileCache& image_file_cache()
        {
            static HDRImageFileCache cache;
            return cache;
        }

        void trim_image_file_cache_locked(HDRImageFileCache& cache)
        {
            constexpr size_t kMaxCachedImages = 4;
            while (cache.order.size() > kMaxCachedImages) {
                cache.images.erase(cache.order.front());
                cache.order.erase(cache.order.begin());
            }
        }
    }

    bool openexr_image_file_identity_key(
        const std::string& path,
        std::string& out,
        std::string& error)
    {
        out.clear();
        error.clear();

        if (path.empty()) {
            error = "OpenEXR image path is empty";
            return false;
        }

        std::error_code ec;
        const std::filesystem::path fs_path{ path };
        const auto size = std::filesystem::file_size(fs_path, ec);
        if (ec) {
            error = "OpenEXR image file size unavailable: " + path;
            return false;
        }

        const auto write_time =
            std::filesystem::last_write_time(fs_path, ec);
        if (ec) {
            error = "OpenEXR image write time unavailable: " + path;
            return false;
        }

        out = path + ":size:" + std::to_string(size)
            + ":write:" + std::to_string(
                write_time.time_since_epoch().count());
        return true;
    }

    bool load_openexr_image_from_memory(
        std::span<const uint8_t> bytes,
        HDRImageData& out,
        std::string& error)
    {
        out = {};
        error.clear();

        if (bytes.empty()) {
            error = "OpenEXR image payload is empty";
            return false;
        }

        float* rgba = nullptr;
        int width = 0;
        int height = 0;
        TinyEXRError tiny_error{};

        const int result = LoadEXRFromMemory(
            &rgba,
            &width,
            &height,
            bytes.data(),
            bytes.size(),
            &tiny_error.message);

        if (result != TINYEXR_SUCCESS) {
            error = tiny_error.message
                ? tiny_error.message
                : "TinyEXR failed to decode OpenEXR image";
            return false;
        }

        if (!rgba || width <= 0 || height <= 0) {
            std::free(rgba);
            error = "TinyEXR returned an invalid OpenEXR image";
            return false;
        }

        const size_t pixel_count =
            static_cast<size_t>(width) * static_cast<size_t>(height);
        HDRImageData decoded{};
        decoded.width = static_cast<uint32_t>(width);
        decoded.height = static_cast<uint32_t>(height);
        decoded.channels = 4;
        decoded.pixels.resize(pixel_count * decoded.channels);
        std::copy(
            rgba,
            rgba + decoded.pixels.size(),
            decoded.pixels.begin());
        std::free(rgba);

        if (!decoded.valid()) {
            error = "decoded OpenEXR image is internally inconsistent";
            return false;
        }

        out = std::move(decoded);
        return true;
    }

    bool save_openexr_image_to_file(
        const std::string& path,
        const HDRImageData& image,
        std::string& error)
    {
        error.clear();

        if (path.empty()) {
            error = "OpenEXR output path is empty";
            return false;
        }
        if (!image.valid()) {
            error = "OpenEXR image to save is invalid";
            return false;
        }
        if (image.channels != 3 && image.channels != 4) {
            error = "OpenEXR save requires 3 (RGB) or 4 (RGBA) channels";
            return false;
        }

        unsigned char* encoded = nullptr;
        TinyEXRError tiny_error{};
        const int size = SaveEXRToMemory(
            image.pixels.data(),
            static_cast<int>(image.width),
            static_cast<int>(image.height),
            static_cast<int>(image.channels),
            0, // save_as_fp16 == 0 -> full fp32
            &encoded,
            &tiny_error.message);

        if (size <= 0 || !encoded) {
            std::free(encoded);
            error = tiny_error.message
                ? tiny_error.message
                : "TinyEXR failed to encode OpenEXR image";
            return false;
        }

        wz::fs::Buffer bytes(encoded, encoded + static_cast<size_t>(size));
        std::free(encoded);

        const wz::fs::FileError write_error =
            wz::fs::write_file(path, bytes, true);
        if (write_error != wz::fs::FileError::None) {
            error = "failed to write OpenEXR file: " + path;
            return false;
        }
        return true;
    }

    bool load_openexr_image_from_file_cached(
        const std::string& path,
        std::shared_ptr<const HDRImageData>& out,
        std::string& error)
    {
        out.reset();
        error.clear();

        if (path.empty()) {
            error = "OpenEXR image path is empty";
            return false;
        }

        std::string cache_key;
        if (!openexr_image_file_identity_key(path, cache_key, error)) {
            return false;
        }

        HDRImageFileCache& cache = image_file_cache();
        {
            std::lock_guard<std::mutex> lock(cache.mutex);
            const auto found = cache.images.find(cache_key);
            if (found != cache.images.end()) {
                out = found->second;
                return out && out->valid();
            }
        }

        const auto bytes = wz::fs::read_file(path);
        if (!bytes) {
            error = "OpenEXR image read failed: " + path;
            return false;
        }

        HDRImageData decoded{};
        if (!load_openexr_image_from_memory(bytes.value, decoded, error)) {
            return false;
        }

        auto shared = std::make_shared<HDRImageData>(std::move(decoded));
        {
            std::lock_guard<std::mutex> lock(cache.mutex);
            cache.images[cache_key] = shared;
            cache.order.push_back(cache_key);
            trim_image_file_cache_locked(cache);
        }

        out = std::move(shared);
        return true;
    }

} // namespace wz::engine::assets
