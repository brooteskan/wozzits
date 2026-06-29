#pragma once

// audio/grain_window.h
//
// Grain amplitude-window shapes for the granular generator (GrainCloud). A window
// is a pure function of a grain's normalized phase (0 at the grain's start, 1 at
// its end) returning a [0, 1] amplitude envelope. Windows go to 0 at both ends so
// grains fade in/out click-free.
//
// EXTENSION POINT: adding a new shape is intentionally a one-file change — add an
// enum value and a case in grain_window_gain(). The set is a closed, bounded enum
// (exhaustiveness-checked by the switch) rather than a function pointer, so the
// shape can be authored as an ordinal in an asset descriptor and stays trivially
// copyable across the audio command queue.

#include <cmath>
#include <cstdint>

namespace wz::audio {

    enum class GrainWindow : uint8_t
    {
        Gaussian = 0,
        // Future: Hann, Tukey, Welch, Triangular — add a value here and a case in
        // grain_window_gain(); nothing else needs to change.
    };

    // Amplitude envelope for a grain at normalized phase `phase01` in [0, 1].
    //
    //   Gaussian: a bell whose width is `param` (sigma, in phase units; ~0.15..0.5,
    //             smaller = narrower/peakier). The raw Gaussian is pedestal-
    //             subtracted and renormalized so it is EXACTLY 0 at phase 0 and 1
    //             and 1 at the center — guaranteeing click-free grain edges for any
    //             sigma.
    //
    // Out-of-range phase clamps to 0 (silent). Unknown enum values fall through to
    // Gaussian so a forward-compatible descriptor never produces NaNs.
    [[nodiscard]] inline float grain_window_gain(
        GrainWindow window,
        float       phase01,
        float       param) noexcept
    {
        if (phase01 <= 0.0f || phase01 >= 1.0f) {
            return 0.0f;
        }

        switch (window) {
        case GrainWindow::Gaussian:
        default: {
            // Clamp sigma away from 0 to avoid a divide-by-zero / infinitely
            // narrow window.
            const double sigma =
                (param > 1.0e-3f) ? static_cast<double>(param) : 1.0e-3;
            const double x = (static_cast<double>(phase01) - 0.5) / sigma;
            const double raw = std::exp(-0.5 * x * x);
            // Value the raw Gaussian would have at the edges (phase 0 / 1).
            const double edge_x = 0.5 / sigma;
            const double edge = std::exp(-0.5 * edge_x * edge_x);
            const double denom = 1.0 - edge;
            const double g =
                (denom > 1.0e-9) ? (raw - edge) / denom : raw;
            return (g > 0.0) ? static_cast<float>(g) : 0.0f;
        }
        }
    }

} // namespace wz::audio
