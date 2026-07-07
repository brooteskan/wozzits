// src/tools/sky_gaussian_bake/main.cpp
//
// wozzits_sky_bake -- fit a spherical-Gaussian mixture to an EXR panorama and
// write it out as <name>.sky_gaussian.json. Pure CPU; no GPU, no engine boot.
//
//   wozzits_sky_bake <input.exr> --out <path.sky_gaussian.json>
//       [--lobes N] [--points N] [--samples N] [--no-log]
//
// Exit codes: 0 ok, 1 runtime error, 2 usage error.

#include <engine/assets/hdri/hdri_image_loader.h>
#include <engine/assets/sky_gaussian/sky_gaussian.h>
#include <engine/assets/sky_gaussian/sky_gaussian_fitter.h>
#include <engine/assets/sky_gaussian/sky_gaussian_serialize.h>

#include <external/json/json_writer.h>
#include <file/filesystem.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace
{
    struct Options
    {
        std::string input;
        std::string out;
        int  lobes = 256;
        int  points = 1;
        int  samples = 8192;
        bool log_domain = true;
    };

    bool parse_options(int argc, char** argv, Options& out, std::string& error)
    {
        if (argc < 2) {
            error =
                "usage: wozzits_sky_bake <input.exr> "
                "--out <path.sky_gaussian.json> "
                "[--lobes N] [--points N] [--samples N] [--no-log]";
            return false;
        }

        out.input = argv[1];

        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--out" && i + 1 < argc) {
                out.out = argv[++i];
            }
            else if (arg == "--lobes" && i + 1 < argc) {
                out.lobes = std::atoi(argv[++i]);
            }
            else if (arg == "--points" && i + 1 < argc) {
                out.points = std::atoi(argv[++i]);
            }
            else if (arg == "--samples" && i + 1 < argc) {
                out.samples = std::atoi(argv[++i]);
            }
            else if (arg == "--no-log") {
                out.log_domain = false;
            }
            else {
                error = "unknown or incomplete option: " + arg;
                return false;
            }
        }

        if (out.input.empty()) {
            error = "missing input EXR path";
            return false;
        }
        if (out.out.empty()) {
            error = "missing required --out path";
            return false;
        }
        return true;
    }
} // namespace

int main(int argc, char** argv)
{
    using namespace wz::engine::assets;

    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cerr << "[error] " << error << "\n";
        return 2;
    }

    std::shared_ptr<const HDRImageData> image;
    std::string load_error;
    if (!load_openexr_image_from_file_cached(options.input, image, load_error)
        || !image)
    {
        std::cerr << "[error] failed to load EXR '" << options.input
                  << "': " << load_error << "\n";
        return 1;
    }
    if (!image->valid()) {
        std::cerr << "[error] EXR '" << options.input
                  << "' decoded to an invalid image\n";
        return 1;
    }

    sky::EquirectSampler sampler;
    sampler.pixels = image->pixels.data();
    sampler.width = static_cast<int>(image->width);
    sampler.height = static_cast<int>(image->height);
    sampler.channels = static_cast<int>(image->channels);

    sky::FitParams params;
    params.target_lobes = options.lobes;
    params.point_source_count = options.points;
    params.sample_count = options.samples;
    params.log_domain_loss = options.log_domain;

    sky::FitReport report;
    sky::SkyGaussianSet set =
        sky::fit_sky_gaussians(sampler, params, &report);
    set.source_name = wz::fs::filename(options.input);
    set.source_width = image->width;
    set.source_height = image->height;

    const wz::json::JSONValue json = sky::sky_gaussian_to_json(set);
    const std::string text =
        wz::json::serialize_json(json, wz::json::JSONWriteOptions{});

    const wz::fs::FileError write_error =
        wz::fs::write_file_text(options.out, text);
    if (write_error != wz::fs::FileError::None) {
        std::cerr << "[error] failed to write output '" << options.out
                  << "'\n";
        return 1;
    }

    std::cout << "[sky_bake] input:            " << options.input << "\n";
    std::cout << "[sky_bake] source:           " << image->width << "x"
              << image->height << " (" << image->channels << " ch)\n";
    std::cout << "[sky_bake] output:           " << options.out << "\n";
    std::cout << "[sky_bake] lobes placed:     " << report.lobes_placed
              << " / " << options.lobes << "\n";
    std::cout << "[sky_bake] point sources:    " << set.point_sources.size()
              << "\n";
    std::cout << "[sky_bake] rms_tonemapped:   " << report.rms_tonemapped
              << "\n";
    std::cout << "[sky_bake] irradiance_error: " << report.irradiance_error
              << "\n";
    std::cout << "[sky_bake] energy_ratio:     " << report.energy_ratio
              << "\n";
    return 0;
}
