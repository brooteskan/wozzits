// src/tools/sky_gaussian_bake/main.cpp
//
// wozzits_sky_bake -- fit a spherical-Gaussian mixture to an EXR panorama and
// write it out as <name>.sky_gaussian.json. Pure CPU; no GPU, no engine boot.
//
//   wozzits_sky_bake <input.exr> --out <path.sky_gaussian.json>
//       [--lobes N] [--points N] [--samples N] [--no-log]
//       [--reconstruct <path.exr>] [--recon-width N]
//
// --reconstruct renders the fitted set back to an equirectangular EXR so the
// fit can be eyeballed against the source panorama (no GPU, no engine boot).
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
        int  refine = 8;
        bool log_domain = true;
        std::string reconstruct;   // optional EXR fit-preview path
        int  recon_width = 1024;   // height is width / 2

        // Creative dials (see FitParams). Defaults reproduce a faithful fit.
        float sharpness_scale = 1.0f;
        float min_sharpness = 4.0f;
        float max_sharpness = 4000.0f;
        float residual_floor = 0.0f;
        float horizon_deg = 0.0f;
        float ground_weight = 1.0f;
        float exposure = 1.0f;
        float saturation = 1.0f;
        float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f;
    };

    bool parse_options(int argc, char** argv, Options& out, std::string& error)
    {
        if (argc < 2) {
            error =
                "usage: wozzits_sky_bake <input.exr> "
                "--out <path.sky_gaussian.json>\n"
                "  structure : [--lobes N] [--samples N] [--refine N] "
                "[--residual-floor F] [--no-log]\n"
                "  points    : [--points N]\n"
                "  crispness : [--sharpness-scale F] [--min-sharpness F] "
                "[--max-sharpness F]\n"
                "  hemisphere: [--horizon DEG] [--ground-weight F]\n"
                "  grade     : [--exposure F] [--saturation F] [--tint R G B]\n"
                "  preview   : [--reconstruct <path.exr>] [--recon-width N]";
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
            else if (arg == "--refine" && i + 1 < argc) {
                out.refine = std::atoi(argv[++i]);
            }
            else if (arg == "--no-log") {
                out.log_domain = false;
            }
            else if (arg == "--residual-floor" && i + 1 < argc) {
                out.residual_floor = static_cast<float>(std::atof(argv[++i]));
            }
            else if (arg == "--sharpness-scale" && i + 1 < argc) {
                out.sharpness_scale = static_cast<float>(std::atof(argv[++i]));
            }
            else if (arg == "--min-sharpness" && i + 1 < argc) {
                out.min_sharpness = static_cast<float>(std::atof(argv[++i]));
            }
            else if (arg == "--max-sharpness" && i + 1 < argc) {
                out.max_sharpness = static_cast<float>(std::atof(argv[++i]));
            }
            else if (arg == "--horizon" && i + 1 < argc) {
                out.horizon_deg = static_cast<float>(std::atof(argv[++i]));
            }
            else if (arg == "--ground-weight" && i + 1 < argc) {
                out.ground_weight = static_cast<float>(std::atof(argv[++i]));
            }
            else if (arg == "--exposure" && i + 1 < argc) {
                out.exposure = static_cast<float>(std::atof(argv[++i]));
            }
            else if (arg == "--saturation" && i + 1 < argc) {
                out.saturation = static_cast<float>(std::atof(argv[++i]));
            }
            else if (arg == "--tint" && i + 3 < argc) {
                out.tint_r = static_cast<float>(std::atof(argv[++i]));
                out.tint_g = static_cast<float>(std::atof(argv[++i]));
                out.tint_b = static_cast<float>(std::atof(argv[++i]));
            }
            else if (arg == "--reconstruct" && i + 1 < argc) {
                out.reconstruct = argv[++i];
            }
            else if (arg == "--recon-width" && i + 1 < argc) {
                out.recon_width = std::atoi(argv[++i]);
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
    params.refine_iterations = options.refine;
    params.log_domain_loss = options.log_domain;
    params.residual_floor = options.residual_floor;
    params.sharpness_scale = options.sharpness_scale;
    params.min_sharpness = options.min_sharpness;
    params.max_sharpness = options.max_sharpness;
    params.horizon_elevation_deg = options.horizon_deg;
    params.ground_weight = options.ground_weight;
    params.exposure = options.exposure;
    params.saturation = options.saturation;
    params.tint = wz::math::Vec3{ options.tint_r, options.tint_g, options.tint_b };

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

    if (!options.reconstruct.empty()) {
        const int rw = options.recon_width > 0 ? options.recon_width : 1024;
        const int rh = rw / 2;
        const HDRImageData preview = sky::reconstruct_equirect(set, rw, rh);
        std::string recon_error;
        if (!save_openexr_image_to_file(
                options.reconstruct, preview, recon_error)) {
            std::cerr << "[error] failed to write reconstruction '"
                      << options.reconstruct << "': " << recon_error << "\n";
            return 1;
        }
        std::cout << "[sky_bake] reconstruction:   " << options.reconstruct
                  << " (" << rw << "x" << rh << ")\n";
    }

    return 0;
}
