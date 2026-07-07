// src/engine/assets/sky_gaussian/sky_gaussian_fitter.cpp
//
// Greedy matching-pursuit fit of a spherical-Gaussian mixture to an
// equirectangular HDR panorama, followed by block coordinate-descent refine.
// Deterministic end-to-end: the only "sampling" is a fixed Fibonacci lattice
// and all argmax / sort ties resolve to the lowest index.

#include <engine/assets/sky_gaussian/sky_gaussian_fitter.h>

#include <math/vec3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace wz::engine::assets::sky
{
    using wz::math::Vec3;

    namespace
    {
        float clampf(float v, float lo, float hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }

        float safe_lum(const Vec3& c)
        {
            const float l = luminance(c);
            return l > 0.0f ? l : 0.0f;
        }

        float tonemap_lum(const Vec3& c, bool log_domain)
        {
            const float l = safe_lum(c);
            return log_domain ? std::log1p(l) : l;
        }

        Vec3 normalize_or(const Vec3& v, const Vec3& fallback)
        {
            const float len =
                std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (len <= 1e-12f) {
                return fallback;
            }
            const float inv = 1.0f / len;
            return Vec3{ v.x * inv, v.y * inv, v.z * inv };
        }

        // Deterministic creative grade applied to a fitted RGB value:
        // exposure (brightness), saturation (chroma around luminance), tint
        // (per-channel colour multiply). Identity at (1, 1, {1,1,1}).
        Vec3 apply_grade(
            const Vec3& c, float exposure, float saturation, const Vec3& tint)
        {
            Vec3 e{ c.x * exposure, c.y * exposure, c.z * exposure };
            const float l = luminance(e);
            e = Vec3{ l + (e.x - l) * saturation,
                      l + (e.y - l) * saturation,
                      l + (e.z - l) * saturation };
            e = Vec3{ e.x * tint.x, e.y * tint.y, e.z * tint.z };
            return Vec3{ std::max(0.0f, e.x),
                         std::max(0.0f, e.y),
                         std::max(0.0f, e.z) };
        }
    } // namespace

    std::vector<Vec3> fibonacci_sphere(int n)
    {
        std::vector<Vec3> out;
        if (n <= 0) {
            return out;
        }
        out.reserve(static_cast<size_t>(n));

        // Golden angle in radians.
        const float golden = kPi * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < n; ++i) {
            const float z = 1.0f
                - (2.0f * static_cast<float>(i) + 1.0f)
                    / static_cast<float>(n);
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float phi = static_cast<float>(i) * golden;
            const float x = r * std::cos(phi);
            const float y = r * std::sin(phi);
            out.push_back(Vec3{ x, y, z });
        }
        return out;
    }

    Vec3 EquirectSampler::sample(const Vec3& dir) const
    {
        if (!pixels || width <= 0 || height <= 0 || channels < 3) {
            return Vec3{ 0.0f, 0.0f, 0.0f };
        }

        const Vec3 d = normalize_or(dir, Vec3{ 0.0f, 1.0f, 0.0f });

        const float theta = std::atan2(d.x, d.z);
        const float phi = std::asin(clampf(d.y, -1.0f, 1.0f));

        float u = theta / kTwoPi + 0.5f;
        float v = 0.5f - phi / kPi;
        u = u - std::floor(u);      // wrap
        v = clampf(v, 0.0f, 1.0f);  // clamp

        const float fx = u * static_cast<float>(width) - 0.5f;
        const float fy = v * static_cast<float>(height) - 0.5f;

        const int x0 = static_cast<int>(std::floor(fx));
        const int y0 = static_cast<int>(std::floor(fy));
        const float tx = fx - static_cast<float>(x0);
        const float ty = fy - static_cast<float>(y0);

        const int w = width;
        const int h = height;
        const int ch = channels;
        const float* px = pixels;

        auto wrap_x = [w](int x) {
            int m = x % w;
            if (m < 0) {
                m += w;
            }
            return m;
        };
        auto clamp_y = [h](int y) {
            if (y < 0) {
                return 0;
            }
            if (y >= h) {
                return h - 1;
            }
            return y;
        };

        const int xi0 = wrap_x(x0);
        const int xi1 = wrap_x(x0 + 1);
        const int yi0 = clamp_y(y0);
        const int yi1 = clamp_y(y0 + 1);

        auto texel = [px, w, ch](int x, int y) {
            const size_t idx =
                (static_cast<size_t>(y) * static_cast<size_t>(w)
                    + static_cast<size_t>(x))
                * static_cast<size_t>(ch);
            return Vec3{ px[idx + 0], px[idx + 1], px[idx + 2] };
        };

        const Vec3 c00 = texel(xi0, yi0);
        const Vec3 c10 = texel(xi1, yi0);
        const Vec3 c01 = texel(xi0, yi1);
        const Vec3 c11 = texel(xi1, yi1);

        const Vec3 top = c00 * (1.0f - tx) + c10 * tx;
        const Vec3 bot = c01 * (1.0f - tx) + c11 * tx;
        return top * (1.0f - ty) + bot * ty;
    }

    SkyGaussianSet fit_sky_gaussians(
        const EquirectSampler& src,
        const FitParams& p,
        FitReport* report)
    {
        SkyGaussianSet result;

        const int n = std::max(64, p.sample_count);
        const std::vector<Vec3> W = fibonacci_sphere(n);
        const float omega = (4.0f * kPi) / static_cast<float>(n);
        const float min_l = p.min_sharpness;
        const float max_l = std::max(p.min_sharpness, p.max_sharpness);
        const bool log_domain = p.log_domain_loss;
        const float sharp_scale = std::max(1e-3f, p.sharpness_scale);

        // Per-sample importance weight. Samples below the horizon elevation get
        // `ground_weight` (0 => a sky-only fit that ignores the ground); this
        // steers both lobe selection and the weighted amplitude solve.
        const float horizon_y = std::sin(
            clampf(p.horizon_elevation_deg, -90.0f, 90.0f) * kPi / 180.0f);
        const float ground_w = clampf(p.ground_weight, 0.0f, 1.0f);
        std::vector<float> wgt(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k) {
            wgt[k] = (W[k].y < horizon_y) ? ground_w : 1.0f;
        }

        // Sampled radiance and running residual.
        std::vector<Vec3> L(static_cast<size_t>(n));
        std::vector<Vec3> R(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k) {
            L[k] = src.sample(W[k]);
            R[k] = L[k];
        }

        // ── Weighted closed-form amplitude for (mu, lambda) against Res ──
        auto solve_amplitude =
            [&](const Vec3& mu, float lambda, const std::vector<Vec3>& Res) {
                Vec3 num{ 0.0f, 0.0f, 0.0f };
                double den = 0.0;
                for (int k = 0; k < n; ++k) {
                    const float g =
                        std::exp(lambda * (wz::math::dot(mu, W[k]) - 1.0f));
                    const float wg = wgt[k] * g;
                    num = num + Res[k] * wg;
                    den += static_cast<double>(wg) * static_cast<double>(g);
                }
                const float inv =
                    static_cast<float>(1.0 / (den + 1e-12));
                Vec3 a = num * inv;
                a.x = std::max(0.0f, a.x);
                a.y = std::max(0.0f, a.y);
                a.z = std::max(0.0f, a.z);
                return a;
            };

        // ── Estimate lambda from the exponential falloff around mu ──
        // For an isolated lobe ln(lum(w)) = ln(A) + lambda*(mu.w - 1); a
        // luminance-weighted linear regression of y=ln(lum) on x=(mu.w - 1)
        // recovers the slope = lambda. Restrict to the bright core so distant
        // lobes do not bias the estimate.
        auto estimate_lambda =
            [&](const Vec3& mu, const std::vector<Vec3>& Res, float peak_lum) {
                const float fallback = clampf(64.0f * sharp_scale, min_l, max_l);
                if (peak_lum <= 0.0f) {
                    return fallback;
                }
                const float cutoff = 0.02f * peak_lum;
                double sw = 0.0, swx = 0.0, swy = 0.0, swxx = 0.0, swxy = 0.0;
                for (int k = 0; k < n; ++k) {
                    const float mdot = wz::math::dot(mu, W[k]);
                    // Restrict to the angular core around mu. Without this, the
                    // bright cores of OTHER well-separated lobes (which sit at
                    // mdot ~ 0, i.e. x ~ -1, yet clear the luminance cutoff)
                    // would corrupt the exponential-falloff slope.
                    if (mdot < 0.8f) {
                        continue;
                    }
                    const float lm = safe_lum(Res[k]);
                    if (lm < cutoff) {
                        continue;
                    }
                    const double x = static_cast<double>(mdot) - 1.0;
                    const double y = std::log(static_cast<double>(lm));
                    const double wgt = static_cast<double>(lm);
                    sw += wgt;
                    swx += wgt * x;
                    swy += wgt * y;
                    swxx += wgt * x * x;
                    swxy += wgt * x * y;
                }
                const double denom = sw * swxx - swx * swx;
                if (std::fabs(denom) < 1e-12 || sw <= 0.0) {
                    return fallback;
                }
                const double slope = (sw * swxy - swx * swy) / denom;
                return clampf(static_cast<float>(slope) * sharp_scale, min_l, max_l);
            };

        // Weighted selection score: bright residual, deprioritised by the
        // per-sample weight so the greedy pass favours the sky over the ground.
        auto sel = [&](int k) {
            const float s = log_domain
                ? std::log1p(safe_lum(R[k]))
                : safe_lum(R[k]);
            return wgt[k] * s;
        };

        // ─────────────────────────────────────────────────────────────
        // (b) Point-source extraction.
        // ─────────────────────────────────────────────────────────────
        const int point_count = std::max(0, p.point_source_count);
        if (point_count > 0) {
            std::vector<float> lum(static_cast<size_t>(n));
            for (int k = 0; k < n; ++k) {
                lum[k] = safe_lum(L[k]);
            }
            std::vector<float> sorted = lum;
            std::sort(sorted.begin(), sorted.end());
            const float pct = clampf(p.point_luminance_percentile, 0.0f, 1.0f);
            int idx = static_cast<int>(
                std::floor(pct * static_cast<float>(n - 1)));
            idx = std::max(0, std::min(n - 1, idx));
            const float threshold = sorted[idx];

            const float neighbor_dot = std::cos(5.0f * kPi / 180.0f);

            // Deterministic local-maximum test: k is a peak if no neighbor
            // strictly outranks it, ranking by (luminance desc, index asc).
            std::vector<int> peaks;
            for (int k = 0; k < n; ++k) {
                if (lum[k] < threshold || lum[k] <= 0.0f) {
                    continue;
                }
                bool is_peak = true;
                for (int j = 0; j < n && is_peak; ++j) {
                    if (j == k) {
                        continue;
                    }
                    if (wz::math::dot(W[k], W[j]) <= neighbor_dot) {
                        continue;
                    }
                    const bool j_outranks =
                        (lum[j] > lum[k])
                        || (lum[j] == lum[k] && j < k);
                    if (j_outranks) {
                        is_peak = false;
                    }
                }
                if (is_peak) {
                    peaks.push_back(k);
                }
            }

            std::sort(peaks.begin(), peaks.end(), [&](int a, int b) {
                if (lum[a] != lum[b]) {
                    return lum[a] > lum[b];
                }
                return a < b;
            });

            const float cap_dot = std::cos(3.0f * kPi / 180.0f);
            const int take =
                std::min<int>(point_count, static_cast<int>(peaks.size()));
            for (int t = 0; t < take; ++t) {
                const int k = peaks[t];
                SkyPointSource ps;
                ps.direction = W[k];
                ps.radiance = L[k];
                ps.solid_angle = omega;
                result.point_sources.push_back(ps);

                for (int j = 0; j < n; ++j) {
                    if (wz::math::dot(W[k], W[j]) > cap_dot) {
                        R[j] = Vec3{ 0.0f, 0.0f, 0.0f };
                    }
                }
            }
        }

        // ─────────────────────────────────────────────────────────────
        // (c) Greedy matching pursuit (non-negative residual).
        //
        // After each subtraction the residual is clamped at zero so a broad
        // lobe's tails cannot carve negative "craters" that erase positive
        // peaks elsewhere. That over-subtraction previously made placement
        // unstable vs sample_count and terminated the fit far short of
        // target_lobes (e.g. 36/1024). It also lets us drop the old
        // lambda-escalation hack: with R >= 0 the amplitude is >= 0 by
        // construction and never collapses on a genuine positive peak.
        // ─────────────────────────────────────────────────────────────
        const int target = std::max(0, p.target_lobes);
        float initial_peak = 0.0f;
        for (int k = 0; k < n; ++k) {
            initial_peak = std::max(initial_peak, sel(k));
        }
        // residual_floor stops the fit early when the brightest remaining
        // residual falls below this fraction of the initial peak -- a creative
        // "abstraction" dial (0 => keep placing until target_lobes).
        const float floor_frac = clampf(p.residual_floor, 0.0f, 1.0f);
        const float stop_score = std::max(1e-8f, floor_frac * initial_peak);

        std::vector<SkyGaussianLobe> lobes;
        lobes.reserve(static_cast<size_t>(target));

        const int max_iter = target + n; // safety bound including skip iters
        for (int iter = 0;
             iter < max_iter && static_cast<int>(lobes.size()) < target;
             ++iter)
        {
            // argmax weighted residual (tie -> lowest index).
            int best_k = -1;
            float best = -1.0f;
            for (int k = 0; k < n; ++k) {
                const float s = sel(k);
                if (s > best) {
                    best = s;
                    best_k = k;
                }
            }
            if (best_k < 0 || best <= stop_score) {
                break;
            }

            const Vec3 mu = W[best_k];
            const float peak_lum = safe_lum(R[best_k]);
            const float lambda = estimate_lambda(mu, R, peak_lum);
            const Vec3 amp = solve_amplitude(mu, lambda, R);

            if (safe_lum(amp) <= 1e-12f) {
                // Degenerate: suppress this peak's neighbourhood and continue
                // rather than terminating the whole fit.
                for (int k = 0; k < n; ++k) {
                    if (wz::math::dot(mu, W[k]) > 0.99f) {
                        R[k] = Vec3{ 0.0f, 0.0f, 0.0f };
                    }
                }
                continue;
            }

            const SkyGaussianLobe lobe{ mu, lambda, amp };
            for (int k = 0; k < n; ++k) {
                const Vec3 r = R[k] - evaluate_lobe(lobe, W[k]);
                R[k] = Vec3{ std::max(0.0f, r.x),
                             std::max(0.0f, r.y),
                             std::max(0.0f, r.z) };
            }
            lobes.push_back(lobe);
        }

        // Recompute the exact residual from the placed lobes: the clamped
        // residual above is only a selection heuristic; refine + report need
        // the true signed residual L - sum(lobes).
        for (int k = 0; k < n; ++k) {
            Vec3 m{ 0.0f, 0.0f, 0.0f };
            for (const SkyGaussianLobe& lb : lobes) {
                m = m + evaluate_lobe(lb, W[k]);
            }
            R[k] = L[k] - m;
        }

        // ─────────────────────────────────────────────────────────────
        // (d) Block coordinate-descent refine.
        // ─────────────────────────────────────────────────────────────
        std::vector<float> tmL(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k) {
            tmL[k] = tonemap_lum(L[k], log_domain);
        }

        std::vector<Vec3> res_excl(static_cast<size_t>(n));
        std::vector<Vec3> base(static_cast<size_t>(n));

        auto compute_err =
            [&](const Vec3& mu, float lam, const Vec3& amp) {
                double e = 0.0;
                for (int k = 0; k < n; ++k) {
                    const float g =
                        std::exp(lam * (wz::math::dot(mu, W[k]) - 1.0f));
                    const Vec3 fit = base[k] + amp * g;
                    const float d = tonemap_lum(fit, log_domain) - tmL[k];
                    e += static_cast<double>(d) * static_cast<double>(d);
                }
                return e;
            };

        const int passes = std::max(0, p.refine_iterations);
        for (int pass = 0; pass < passes; ++pass) {
            for (size_t i = 0; i < lobes.size(); ++i) {
                const SkyGaussianLobe cur = lobes[i];

                // Add lobe i back into the residual conceptually.
                Vec3 centroid{ 0.0f, 0.0f, 0.0f };
                float wsum = 0.0f;
                for (int k = 0; k < n; ++k) {
                    const Vec3 g = evaluate_lobe(cur, W[k]);
                    res_excl[k] = R[k] + g;
                    base[k] = L[k] - res_excl[k];
                    if (wz::math::dot(cur.direction, W[k]) > 0.5f) {
                        const float lm = safe_lum(res_excl[k]);
                        centroid = centroid + W[k] * lm;
                        wsum += lm;
                    }
                }

                Vec3 best_mu = cur.direction;
                float best_lam = clampf(cur.sharpness, min_l, max_l);
                Vec3 best_amp = solve_amplitude(best_mu, best_lam, res_excl);
                double best_err = compute_err(best_mu, best_lam, best_amp);

                const Vec3 mu_ref = wsum > 1e-12f
                    ? normalize_or(centroid, cur.direction)
                    : cur.direction;

                const std::array<Vec3, 2> mus = { cur.direction, mu_ref };
                const std::array<float, 3> lams = {
                    clampf(cur.sharpness, min_l, max_l),
                    clampf(cur.sharpness * 0.8f, min_l, max_l),
                    clampf(cur.sharpness * 1.25f, min_l, max_l),
                };

                for (const Vec3& mu : mus) {
                    for (float lam : lams) {
                        const Vec3 amp =
                            solve_amplitude(mu, lam, res_excl);
                        const double err = compute_err(mu, lam, amp);
                        if (err < best_err - 1e-9) {
                            best_err = err;
                            best_mu = mu;
                            best_lam = lam;
                            best_amp = amp;
                        }
                    }
                }

                const SkyGaussianLobe nb{ best_mu, best_lam, best_amp };
                for (int k = 0; k < n; ++k) {
                    R[k] = res_excl[k] - evaluate_lobe(nb, W[k]);
                }
                lobes[i] = nb;
            }
        }

        result.lobes = std::move(lobes);

        // ─────────────────────────────────────────────────────────────
        // (e) Report.
        // ─────────────────────────────────────────────────────────────
        if (report) {
            // model M_k = L_k - R_k.
            double sse = 0.0;
            for (int k = 0; k < n; ++k) {
                const Vec3 m = L[k] - R[k];
                const float d =
                    tonemap_lum(m, log_domain) - tonemap_lum(L[k], log_domain);
                sse += static_cast<double>(d) * static_cast<double>(d);
            }
            report->rms_tonemapped =
                static_cast<float>(std::sqrt(sse / static_cast<double>(n)));

            const std::vector<Vec3> normals = fibonacci_sphere(64);
            double irr_acc = 0.0;
            int irr_cnt = 0;
            for (const Vec3& nrm : normals) {
                Vec3 e_src{ 0.0f, 0.0f, 0.0f };
                Vec3 e_fit{ 0.0f, 0.0f, 0.0f };
                for (int k = 0; k < n; ++k) {
                    float c = wz::math::dot(nrm, W[k]);
                    if (c < 0.0f) {
                        c = 0.0f;
                    }
                    const float ww = omega * c;
                    e_src = e_src + L[k] * ww;
                    e_fit = e_fit + (L[k] - R[k]) * ww;
                }
                const float ls = luminance(e_src);
                const float lf = luminance(e_fit);
                if (ls > 1e-8f) {
                    irr_acc += std::fabs(lf - ls) / ls;
                    ++irr_cnt;
                }
            }
            report->irradiance_error = irr_cnt > 0
                ? static_cast<float>(irr_acc / static_cast<double>(irr_cnt))
                : 0.0f;

            Vec3 s_src{ 0.0f, 0.0f, 0.0f };
            Vec3 s_fit{ 0.0f, 0.0f, 0.0f };
            for (int k = 0; k < n; ++k) {
                s_src = s_src + L[k] * omega;
                s_fit = s_fit + (L[k] - R[k]) * omega;
            }
            const float lss = luminance(s_src);
            report->energy_ratio =
                lss > 1e-8f ? luminance(s_fit) / lss : 0.0f;

            report->lobes_placed = static_cast<int>(result.lobes.size());
        }

        // Creative grade (deterministic): applied AFTER the fidelity report, so
        // the report measures fit accuracy while exposure/saturation/tint are a
        // deliberate artistic deviation baked into the result.
        const bool graded = p.exposure != 1.0f || p.saturation != 1.0f
            || p.tint.x != 1.0f || p.tint.y != 1.0f || p.tint.z != 1.0f;
        if (graded) {
            for (SkyGaussianLobe& lb : result.lobes) {
                lb.amplitude =
                    apply_grade(lb.amplitude, p.exposure, p.saturation, p.tint);
            }
            for (SkyPointSource& ps : result.point_sources) {
                ps.radiance =
                    apply_grade(ps.radiance, p.exposure, p.saturation, p.tint);
            }
        }

        return result;
    }

    HDRImageData reconstruct_equirect(
        const SkyGaussianSet& set, int width, int height)
    {
        HDRImageData image{};
        if (width <= 0 || height <= 0) {
            return image;
        }
        image.width = static_cast<uint32_t>(width);
        image.height = static_cast<uint32_t>(height);
        image.channels = 3;
        image.pixels.assign(
            static_cast<size_t>(width) * static_cast<size_t>(height) * 3u, 0.0f);

        // A point source is near-delta; floor its cap to a couple of texels so
        // the sun renders as a visible disk rather than vanishing sub-pixel.
        const float texel_angle = kPi / static_cast<float>(height);
        const float max_cos = std::cos(2.0f * texel_angle);
        struct Cap { Vec3 dir; Vec3 radiance; float cos_r; };
        std::vector<Cap> caps;
        caps.reserve(set.point_sources.size());
        for (const SkyPointSource& ps : set.point_sources) {
            // Cap solid angle Omega = 2*pi*(1 - cos r)  ->  cos r = 1 - Omega/2pi.
            float cos_r = clampf(1.0f - ps.solid_angle / kTwoPi, -1.0f, 1.0f);
            cos_r = std::min(cos_r, max_cos); // enforce the visibility floor
            caps.push_back({ ps.direction, ps.radiance, cos_r });
        }

        for (int py = 0; py < height; ++py) {
            const float v =
                (static_cast<float>(py) + 0.5f) / static_cast<float>(height);
            const float phi = (0.5f - v) * kPi;
            const float sp = std::sin(phi);
            const float cp = std::cos(phi);
            for (int px = 0; px < width; ++px) {
                const float u =
                    (static_cast<float>(px) + 0.5f) / static_cast<float>(width);
                const float theta = (u - 0.5f) * kTwoPi;
                const Vec3 dir{ cp * std::sin(theta), sp, cp * std::cos(theta) };

                Vec3 c = evaluate_set(set, dir);
                for (const Cap& cap : caps) {
                    if (wz::math::dot(dir, cap.dir) >= cap.cos_r) {
                        c = c + cap.radiance;
                    }
                }

                const size_t idx =
                    (static_cast<size_t>(py) * static_cast<size_t>(width)
                        + static_cast<size_t>(px)) * 3u;
                image.pixels[idx + 0] = c.x;
                image.pixels[idx + 1] = c.y;
                image.pixels[idx + 2] = c.z;
            }
        }

        return image;
    }

} // namespace wz::engine::assets::sky
