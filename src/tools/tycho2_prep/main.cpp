// src/tools/tycho2_prep/main.cpp
//
// wozzits_tycho2_prep -- stage 1 of the Tycho-2 star field (issue #266). Parses
// the Tycho-2 main catalogue (CDS I/259 catalog.dat, ~2.5M fixed-width ASCII
// records) once and bakes a compact binary PLY point cloud: one vertex per star
// with xyz = unit celestial direction plus vmag / bv properties. Stage 2 (the
// StarCatalogFromPLY graph node) imports that blob with the creative dials.
//
// Doing the expensive ASCII decode offline keeps the runtime import a fast
// binary read, and the PLY is a real point cloud -- droppable into any viewer.
//
//   wozzits_tycho2_prep --in catalog.dat --out tycho2.ply [--max-mag 11.0]
//                       [--limit N]
//
// --max-mag culls faint stars (the size/perf lever); --limit caps the count
// (for a quick slice). Input may also be several concatenated tyc2 files piped
// via --in - (stdin).

#include <engine/starfield/star_catalog.h>
#include <engine/starfield/tycho2.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{
    namespace sf = wz::engine::starfield;

    struct Options
    {
        std::string in_path;
        std::string out_path;
        double max_mag = 1e30;
        uint64_t limit = 0;   // 0 = no limit
    };

    struct Vertex
    {
        float x, y, z;
        float vmag;
        float bv;
    };

    bool parse_args(int argc, char** argv, Options& out, std::string& error)
    {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto next = [&](const char* name) -> std::string {
                if (i + 1 >= argc) {
                    error = std::string("missing value for ") + name;
                    return {};
                }
                return argv[++i];
            };
            if (arg == "--in") {
                out.in_path = next("--in");
            } else if (arg == "--out") {
                out.out_path = next("--out");
            } else if (arg == "--max-mag") {
                out.max_mag = std::strtod(next("--max-mag").c_str(), nullptr);
            } else if (arg == "--limit") {
                out.limit = std::strtoull(next("--limit").c_str(), nullptr, 10);
            } else {
                error = "unknown argument: " + arg;
                return false;
            }
            if (!error.empty()) {
                return false;
            }
        }
        if (out.in_path.empty() || out.out_path.empty()) {
            error = "usage: wozzits_tycho2_prep --in <catalog.dat> --out "
                    "<tycho2.ply> [--max-mag N] [--limit N]";
            return false;
        }
        return true;
    }

    // Minimal binary-little-endian PLY writer (x,y,z,vmag,bv float32 vertices).
    bool write_ply(const std::string& path, const std::vector<Vertex>& verts)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << "ply\n"
            << "format binary_little_endian 1.0\n"
            << "comment wozzits_tycho2_prep\n"
            << "element vertex " << verts.size() << "\n"
            << "property float x\n"
            << "property float y\n"
            << "property float z\n"
            << "property float vmag\n"
            << "property float bv\n"
            << "end_header\n";
        out.write(
            reinterpret_cast<const char*>(verts.data()),
            static_cast<std::streamsize>(verts.size() * sizeof(Vertex)));
        return out.good();
    }
}

int main(int argc, char** argv)
{
    Options opts;
    std::string error;
    if (!parse_args(argc, argv, opts, error)) {
        std::cerr << error << "\n";
        return 2;
    }

    std::ifstream in(opts.in_path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "failed to open input: " << opts.in_path << "\n";
        return 1;
    }

    std::vector<Vertex> verts;
    uint64_t read = 0, kept = 0;
    std::string line;
    while (std::getline(in, line)) {
        ++read;
        const std::optional<sf::CatalogRecord> record =
            sf::parse_tycho2_line(line);
        if (!record) {
            continue;
        }
        if (record->vmag > opts.max_mag) {
            continue;
        }
        const wz::math::Vec3 dir =
            sf::direction_from_ra_dec(record->ra_hours, record->dec_deg);
        verts.push_back(Vertex{
            dir.x, dir.y, dir.z,
            static_cast<float>(record->vmag),
            static_cast<float>(record->has_bv ? record->bv : 0.0),
        });
        ++kept;
        if (opts.limit != 0 && kept >= opts.limit) {
            break;
        }
    }

    if (verts.empty()) {
        std::cerr << "no stars parsed from " << opts.in_path
                  << " (read " << read << " lines)\n";
        return 1;
    }
    if (!write_ply(opts.out_path, verts)) {
        std::cerr << "failed to write PLY: " << opts.out_path << "\n";
        return 1;
    }

    std::cout << "tycho2_prep: read " << read << " lines, wrote " << kept
              << " stars -> " << opts.out_path << "\n";
    return 0;
}
