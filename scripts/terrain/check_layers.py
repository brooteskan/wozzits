"""Check a near/far terrain layer pair for punch-through.

The far terrain is its own clipmap draw, sitting UNDERNEATH the near one rather
than joining it. Nothing enforces that: both are opaque surfaces and the depth
test simply shows whichever is nearer the eye. So wherever the far surface rises
ABOVE the near one inside the near layer's reach, it punches through the ground
the player is standing on -- distant mountains erupting out of a valley floor.

That is the one failure mode this architecture has, it is entirely a property of
the two authored fields, and it is cheap to check here rather than hunt in the
viewport. The rule is simply:

    far_surface(x, z) < near_surface(x, z)   everywhere the near layer draws

The near layer draws out to its clipmap horizon measured from the CAMERA, and
the camera is fenced, so the region to check is the fence area grown by the near
horizon -- computed for you by --fence and --near-horizon.

    python check_layers.py --near near.r32 --near-res 4096 \
        --near-origin -2000 --near-extent 4000 --near-vertical 900 \
        --far far.r32 --far-res 2048 \
        --far-origin -20000 --far-extent 40000 --far-vertical 2000 \
        --fence 1000 --near-horizon 3000

--selftest builds a clean pair and a deliberately erupting one and checks that
each is judged correctly. Run it after changing anything here.
"""
import argparse

import numpy as np


def load(path: str, res: int) -> np.ndarray:
    values = np.fromfile(path, dtype=np.float32)
    if values.size != res * res:
        raise SystemExit(
            f"{path}: {values.size} floats, expected {res * res} for {res}^2")
    return values.reshape(res, res)


def sample_bilinear(field: np.ndarray, wx: np.ndarray, wz: np.ndarray,
                    origin: float, extent: float) -> np.ndarray:
    """Sample a field in the engine's placement-driven convention: `resolution`
    cells span the extent, so texel i sits at origin + i*extent/resolution."""
    res = field.shape[0]
    u = (wx - origin) / extent * res
    v = (wz - origin) / extent * res
    x0 = np.clip(np.floor(u).astype(np.int64), 0, res - 1)
    z0 = np.clip(np.floor(v).astype(np.int64), 0, res - 1)
    x1 = np.clip(x0 + 1, 0, res - 1)
    z1 = np.clip(z0 + 1, 0, res - 1)
    fx = np.clip(u - np.floor(u), 0.0, 1.0)
    fz = np.clip(v - np.floor(v), 0.0, 1.0)
    h0 = field[z0, x0] * (1 - fx) + field[z0, x1] * fx
    h1 = field[z1, x0] * (1 - fx) + field[z1, x1] * fx
    return h0 * (1 - fz) + h1 * fz


def check(near, near_origin, near_extent, near_vertical,
          far, far_origin, far_extent, far_vertical,
          reach, samples=1024):
    """Compare the two surfaces in metres over the region the near layer draws."""
    axis = np.linspace(-reach, reach, samples)
    wx, wz = np.meshgrid(axis, axis, indexing="xy")

    near_m = sample_bilinear(near, wx, wz, near_origin, near_extent) * near_vertical
    far_m = sample_bilinear(far, wx, wz, far_origin, far_extent) * far_vertical
    clearance = near_m - far_m           # want > 0 everywhere

    worst = float(clearance.min())
    frac = float((clearance <= 0.0).mean())
    idx = np.unravel_index(np.argmin(clearance), clearance.shape)
    return {
        "worst_clearance": worst,
        "punch_fraction": frac,
        "worst_at": (float(wx[idx]), float(wz[idx])),
        "worst_near": float(near_m[idx]),
        "worst_far": float(far_m[idx]),
        "median_clearance": float(np.median(clearance)),
    }


def report(stats: dict, margin: float) -> bool:
    print(f"  region checked          +/-{stats.get('reach', 0):.0f} m")
    print(f"  median clearance        {stats['median_clearance']:10.1f} m")
    print(f"  worst clearance         {stats['worst_clearance']:10.1f} m")
    print(f"  at                      ({stats['worst_at'][0]:.0f}, "
          f"{stats['worst_at'][1]:.0f})  near {stats['worst_near']:.0f} m, "
          f"far {stats['worst_far']:.0f} m")
    print(f"  area with far >= near   {100.0 * stats['punch_fraction']:9.2f} %")

    if stats["worst_clearance"] <= 0.0:
        print("\nFAIL punch-through: the far terrain rises above the near terrain")
        print("     inside the near layer's reach, so it will erupt through the")
        print("     ground. Lower the far field's inner region -- it is never")
        print("     seen there, so author it as a basin.")
        return False
    if stats["worst_clearance"] < margin:
        print(f"\nMARGINAL: clears by only {stats['worst_clearance']:.1f} m "
              f"(< {margin:.0f} m margin).")
        print("     Depth precision and the far layer's own LOD error could still")
        print("     let it show. Lower the far field's inner region further.")
        return False
    print(f"\nPASS: the far surface stays at least "
          f"{stats['worst_clearance']:.0f} m below the near one.")
    return True


def selftest() -> int:
    res_n, res_f = 256, 128
    ax_n = np.linspace(-1, 1, res_n)
    xn, zn = np.meshgrid(ax_n, ax_n, indexing="xy")
    near = (0.3 + 0.2 * np.sin(3 * xn) * np.cos(3 * zn)).astype(np.float32)

    ax_f = np.linspace(-1, 1, res_f)
    xf, zf = np.meshgrid(ax_f, ax_f, indexing="xy")
    r = np.sqrt(xf ** 2 + zf ** 2)

    # Good: a basin in the middle rising to mountains outside the near reach.
    far_ok = (0.02 + 0.9 * np.clip((r - 0.35) / 0.4, 0, 1)).astype(np.float32)
    # Bad: mountains all the way in.
    far_bad = (0.5 + 0.4 * np.sin(5 * xf)).astype(np.float32)

    failures = 0
    for label, far, want_pass in (("basin", far_ok, True),
                                  ("erupting", far_bad, False)):
        stats = check(near, -1000.0, 2000.0, 900.0,
                      far, -4000.0, 8000.0, 2000.0,
                      reach=300.0, samples=256)
        got_pass = stats["worst_clearance"] > 20.0
        mark = "ok" if got_pass == want_pass else "WRONG"
        print(f"  {label:10s} worst clearance {stats['worst_clearance']:9.1f} m"
              f"  -> {'pass' if got_pass else 'fail'}  [{mark}]")
        if got_pass != want_pass:
            failures += 1

    print("SELFTEST PASS" if failures == 0 else f"SELFTEST: {failures} wrong")
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--near"), ap.add_argument("--far")
    ap.add_argument("--near-res", type=int, default=4096)
    ap.add_argument("--far-res", type=int, default=2048)
    ap.add_argument("--near-origin", type=float, default=-2000.0)
    ap.add_argument("--far-origin", type=float, default=-20000.0)
    ap.add_argument("--near-extent", type=float, default=4000.0)
    ap.add_argument("--far-extent", type=float, default=40000.0)
    ap.add_argument("--near-vertical", type=float, default=900.0)
    ap.add_argument("--far-vertical", type=float, default=2000.0)
    ap.add_argument("--fence", type=float, default=1000.0,
                    help="half-extent the player is fenced to")
    ap.add_argument("--near-horizon", type=float, default=3000.0,
                    help="near clipmap draw radius")
    ap.add_argument("--margin", type=float, default=50.0,
                    help="required clearance (metres)")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.near or not args.far:
        ap.error("--near and --far are required (or use --selftest)")

    # The near layer draws `near_horizon` from the camera, and the camera can be
    # anywhere inside the fence, so the far layer must stay below it out to the
    # sum of the two.
    reach = args.fence + args.near_horizon
    stats = check(load(args.near, args.near_res),
                  args.near_origin, args.near_extent, args.near_vertical,
                  load(args.far, args.far_res),
                  args.far_origin, args.far_extent, args.far_vertical,
                  reach)
    stats["reach"] = reach
    print("=== near/far layer punch-through ===")
    return 0 if report(stats, args.margin) else 1


if __name__ == "__main__":
    raise SystemExit(main())
