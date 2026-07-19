"""Plan the near/far heightfield pair for the two-layer clipmap.

The two layers are joined by FOG, not geometry (David: "visual blend with the
near clipmap, but not part of"), so they are sized INDEPENDENTLY. There is no
crop-and-upscale step making their surfaces agree at a shared boundary -- an
earlier version of this script assumed one, and derived the near field by
cropping the far field's centre. That is gone.

What the engine actually does per layer (clipmap_view.cpp,
rhi_scene_renderer.cpp near line 1740, resolve_clipmap_lattice):

    c0  (finest lattice cell, metres) = schedule.world_extent / N   # N = field res
    field footprint (visible size)    = Placement.extent            # NOT world_extent
    lattice reach (radius, metres)    = horizon, grown in rings to fit the budget
    field XZ anchor                   = Placement.origin (a corner)

world_extent and footprint are DIFFERENT inputs doing DIFFERENT jobs, and the
earlier script's bug was conflating them -- it computed c0 from the footprint, so
every lattice figure was off by the footprint:world_extent ratio. world_extent
sets how coarse the lattice CELLS are (cost / reach); footprint sets how big the
terrain LOOKS. They are independent: test_mesh_001's near layer draws a 1000 m
footprint with world_extent 2000, so its lattice cells are 2x its field texels.

(Footprint = Placement.extent holds when a Placement asset is connected, as both
these layers do. With no Placement, the renderer falls back to footprint =
world_extent -- the node-transform derivation.)

    python plan_fields.py                       # the real test_mesh_001 pair
    python plan_fields.py --far-horizon 6000 --far-budget 30000

resolve_lattice mirrors resolve_clipmap_lattice in
src/engine/assets/mesh/clipmap_lattice_mesh.cpp (verified field-for-field).
If that changes, this must.
"""
import argparse
import math

K_L_MAX = 24


def resolve_lattice(horizon_m: float, budget: int, c0: float):
    """Port of resolve_clipmap_lattice: smallest ring count that fits budget.

    Returns (level_count, base_resolution, triangles, achieved_horizon_m).
    """
    def resolution_for(level_count: int) -> int:
        coarsest = 2.0 ** (level_count - 1)
        needed_half = horizon_m / (coarsest * c0)
        target_half = math.ceil(needed_half)
        if not target_half >= 1:
            target_half = 1
        return 2 * int(target_half)

    def triangles(m: int, level_count: int) -> int:
        h = m // 2
        q = h // 2
        return 2 * m * m + (level_count - 1) * 8 * (h * h - q * q)

    cheapest = (1, resolution_for(1))
    cheapest_tris = triangles(cheapest[1], 1)
    for level_count in range(1, K_L_MAX + 1):
        m = resolution_for(level_count)
        tris = triangles(m, level_count)
        if tris <= budget:
            return level_count, m, tris, (m // 2) * (2.0 ** (level_count - 1)) * c0
        if tris < cheapest_tris:
            cheapest, cheapest_tris = (level_count, m), tris
    level_count, m = cheapest
    return (level_count, m, cheapest_tris,
            (m // 2) * (2.0 ** (level_count - 1)) * c0)


def describe_layer(tag, res, world_extent, footprint, horizon, budget, centre):
    """Resolve one layer and print what the engine will make of it."""
    c0 = world_extent / res
    level_count, m, tris, achieved = resolve_lattice(horizon, budget, c0)
    field_texel = footprint / res
    # How finely the lattice samples the field. 1.0 = one lattice cell per field
    # texel (the sweet spot); > 1 = coarser than the field (undersampled, cheap);
    # < 1 = finer than the field (drawing detail the field does not carry).
    sample_ratio = c0 / field_texel  # == world_extent / footprint
    origin = centre - footprint / 2.0
    half = footprint / 2.0
    skirt = max(0.0, horizon - half)

    print(f"=== {tag} layer ===")
    print(f"  field      {res}^2, footprint {footprint:.0f} m "
          f"({field_texel:.3f} m/texel), {res * res * 4 / 1e6:.1f} MB")
    print(f"  placement  origin_x/z = {origin:.1f}   extent_x/z = {footprint:.1f}   "
          f"(centred on {centre:.0f})")
    print(f"  schedule   world_extent {world_extent:.0f} m, horizon {horizon:.0f} m, "
          f"budget {budget}")
    print(f"  -> c0 (finest cell)  = {c0:.3f} m   "
          f"[lattice cell : field texel = {sample_ratio:.2f}x]")
    if sample_ratio > 4.0:
        print(f"     NOTE: lattice is {sample_ratio:.0f}x coarser than the field -- "
              f"most of the {res}^2 is never sampled; a smaller field would do.")
    elif sample_ratio < 0.5:
        print(f"     NOTE: lattice is finer than the field -- drawing sub-texel "
              f"detail the {res}^2 field does not carry.")
    print(f"  -> lattice           = {level_count} rings, base_res {m}, "
          f"{tris} triangles")
    short = "" if achieved >= horizon - 0.5 else f"  <-- SHORT of {horizon:.0f}"
    print(f"  -> reaches           = {achieved:.0f} m radius{short}")
    print(f"  -> coarsest cell     = {c0 * 2 ** (level_count - 1):.1f} m")
    if skirt > 0.5:
        print(f"  -> clamped skirt     = field ends at +/-{half:.0f} m, lattice "
              f"draws to +/-{horizon:.0f} m: {skirt:.0f} m of edge-clamp beyond it")
    else:
        print(f"  -> no skirt          = field fills the lattice to +/-{horizon:.0f} m")
    return {"tris": tris, "half": half, "origin": origin, "achieved": achieved}


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    # Defaults ARE test_mesh_001 as it stands today. A bare run prints the truth
    # about the shipping scene; override a flag to explore a change.
    ap.add_argument("--centre", type=float, default=500.0,
                    help="world XZ both fields centre on (near field's centre)")
    ap.add_argument("--fence", type=float, default=500.0,
                    help="half-extent the player is fenced to")

    ap.add_argument("--near-res", type=int, default=4096,
                    help="near field resolution per side (texels)")
    ap.add_argument("--near-world-extent", type=float, default=2000.0,
                    help="near schedule world_extent (drives c0 = /N)")
    ap.add_argument("--near-footprint", type=float, default=1000.0,
                    help="near Placement extent (visible field size)")
    ap.add_argument("--near-horizon", type=float, default=2000.0,
                    help="near clipmap draw radius (metres)")
    ap.add_argument("--near-budget", type=int, default=200000,
                    help="near lattice triangle budget")

    ap.add_argument("--far-res", type=int, default=2048,
                    help="far field resolution per side (texels)")
    ap.add_argument("--far-world-extent", type=float, default=16000.0,
                    help="far schedule world_extent (drives c0 = /N)")
    ap.add_argument("--far-footprint", type=float, default=16000.0,
                    help="far Placement extent (visible field size)")
    ap.add_argument("--far-horizon", type=float, default=8000.0,
                    help="far clipmap draw radius (metres)")
    ap.add_argument("--far-budget", type=int, default=50000,
                    help="far lattice triangle budget")
    args = ap.parse_args()

    near = describe_layer("NEAR", args.near_res, args.near_world_extent,
                          args.near_footprint, args.near_horizon,
                          args.near_budget, args.centre)
    print()
    far = describe_layer("FAR", args.far_res, args.far_world_extent,
                         args.far_footprint, args.far_horizon,
                         args.far_budget, args.centre)

    print(f"\n  TOTAL triangles, both layers: {near['tris'] + far['tris']}"
          f"   (two draws; each budget is per-layer, not shared)")

    # ── The seam the fog has to carry ────────────────────────────────────────
    #
    # The near layer draws near_horizon from a camera that can sit anywhere
    # inside the fence, so the far surface must stay BELOW the near one out to
    # fence + near_horizon. Inside that radius the far field is never the visible
    # ground -- author it as a basin so nothing erupts through the near terrain.
    reach = args.fence + args.near_horizon
    basin_frac = reach / far["half"] if far["half"] > 0 else float("inf")

    print("\n=== the seam (fog joins the layers here) ===")
    print(f"  near draws {args.near_horizon:.0f} m from a camera fenced to "
          f"+/-{args.fence:.0f} m")
    print(f"  -> far must stay BELOW near out to +/-{reach:.0f} m "
          f"(= fence + near horizon)")
    print(f"  -> far basin_radius >= {basin_frac:.2f} "
          f"(fraction of the far half-width {far['half']:.0f} m)")

    if far["half"] <= reach:
        print(f"  !! far footprint half ({far['half']:.0f} m) does NOT clear the "
              f"near coverage ({reach:.0f} m):")
        print( "     the whole far field sits inside the near layer and is never "
               "seen. Enlarge --far-footprint.")
    elif far["achieved"] <= reach:
        print(f"  !! far draws to {far['achieved']:.0f} m but near covers to "
              f"{reach:.0f} m: the far ring is hidden. Raise --far-horizon.")
    else:
        band_hi = min(far["half"], far["achieved"])
        print(f"  -> far terrain reads in the band +/-{reach:.0f}..{band_hi:.0f} m; "
              f"basin covers the inner {basin_frac:.2f} of the field")
        print(f"     verify against the real .r32 with: check_layers.py "
              f"--fence {args.fence:.0f} --near-horizon {args.near_horizon:.0f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
