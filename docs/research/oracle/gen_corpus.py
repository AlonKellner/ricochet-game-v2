"""Generate the precision benchmark corpus and its ground truth.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_corpus.py [--count N] [--seed S] [--dps D]

Writes bench/scenarios/<name>.json and bench/expected/<name>.json.

Scenarios are player-style plans in the 3x3 inversion grid (and radius
variants): random surface sequences (mostly without immediate repeats, so the
inversions do not cancel), random player and cursor. Each scenario is replayed
with the exact (image-derived) direction and kept only if the replay applies
exactly the plan and reaches the cursor (in practice: always).
"""

from __future__ import annotations

import argparse
import os
import random
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from beam import (Beam, PLANNED, PHYSICAL, circle_hits, exact_direction,
                  hit_sensitivity, invert_beam, side_of, sort_key, trace, aim_image)
from scenarios import (grid_circles, mpc, parse_dump, plan_tuples, save,
                       to_mp_circles, GRID_CENTERS)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SCN_DIR = os.path.join(ROOT, "bench", "scenarios")
EXP_DIR = os.path.join(ROOT, "bench", "expected")
SCREEN = (0.0, 0.0, 1920.0, 1080.0)
DIGITS = 40


def s(x):
    return mp.nstr(x, DIGITS, strip_zeros=False)


def in_screen(z, margin=10):
    return (SCREEN[0] + margin <= z.real <= SCREEN[2] - margin and
            SCREEN[1] + margin <= z.imag <= SCREEN[3] - margin)


def outside_all(z, circles, margin=2):
    return all(abs(z - c.c) > c.R + margin for c in circles)


def f32(x):
    import struct
    return struct.unpack("f", struct.pack("f", float(x)))[0]


def random_point(rng, circles, margin):
    while True:
        z = mp.mpc(f32(rng.uniform(40, 1880)), f32(rng.uniform(40, 1040)))
        if outside_all(z, circles, margin):
            return z


def generate_one(rng, circles, depth, chain):
    """A player-style plan: random surfaces (chain=True forbids immediate
    repeats, i.e. no cancelling pairs), sides mostly LEFT, random player and
    cursor. Every such plan is realizable in exact planned-mode arithmetic
    (each beam passes through a point inside the next plan circle)."""
    ids = [c.id for c in circles]
    plan = []
    while len(plan) < depth:
        sid = rng.choice(ids)
        if chain and plan and plan[-1][0] == sid:
            continue
        plan.append((sid, "LEFT" if rng.random() < 0.85 else "RIGHT"))
    return random_point(rng, circles, 5), random_point(rng, circles, 1), plan


N_PHYSICAL = 16


def expected_for(scn, circles, dps_check=None):
    P, C, plan = mpc(scn["player"]), mpc(scn["cursor"]), plan_tuples(scn)
    d = exact_direction(circles, plan, P, C)
    if d == 0:
        return None
    n_planned = 64
    r = trace(circles, P, d, plan, C, PLANNED, PHYSICAL, max_events=n_planned)
    kinds = [e.kind for e in r.events]
    if "cursor" not in kinds:
        return None
    ci = kinds.index("cursor")
    applied = [(e.surface_id, e.side) for e in r.events[:ci] if e.kind == "hit" and e.applied]
    if applied != list(plan):
        return None
    n_keep = min(len(r.events), ci + 5)
    sens = hit_sensitivity(circles, P, d, n_keep, plan, C, PLANNED)
    phys = trace(circles, P, d, plan, C, PHYSICAL, PHYSICAL, max_events=N_PHYSICAL)
    phys_sens = hit_sensitivity(circles, P, d, N_PHYSICAL, plan, C, PHYSICAL)
    u = d / abs(d)
    img = aim_image(circles, plan, C)

    def ev(e, sv):
        o = {"kind": e.kind, "surface_id": e.surface_id, "side": e.side,
             "applied": e.applied}
        if e.point is not None:
            o["x"] = s(e.point.real)
            o["y"] = s(e.point.imag)
        o["sens"] = float(sv) if sv is not None else None
        return o

    return {
        "name": scn["name"],
        "dps": mp.mp.dps,
        "direction": [s(u.real), s(u.imag)],
        "image": [s(img.real), s(img.imag)],
        "cursor_index": ci,
        "planned": [ev(e, sv) for e, sv in zip(r.events[:n_keep], sens)],
        "physical": [ev(e, sv) for e, sv in zip(phys.events, phys_sens)],
        "max_sens_to_cursor": float(max([x for x in sens[:ci + 1] if x is not None] or [0.0])),
    }


def stable_under_precision(scn, circles_fn, exp):
    """Oracle self-check: recompute at double dps and compare event points."""
    old = mp.mp.dps
    mp.mp.dps = old * 2
    try:
        exp2 = expected_for(scn, circles_fn(scn))
    finally:
        mp.mp.dps = old
    if exp2 is None:
        return False, float("inf")
    worst = mp.mpf(0)
    for a, b in zip(exp["planned"], exp2["planned"]):
        if a["kind"] != b["kind"] or a["surface_id"] != b["surface_id"]:
            return False, float("inf")
        if "x" in a:
            worst = max(worst, abs(mp.mpf(a["x"]) - mp.mpf(b["x"])),
                        abs(mp.mpf(a["y"]) - mp.mpf(b["y"])))
    return worst < mp.mpf(10) ** (-30), float(worst)


VARIANTS = {
    "grid60": dict(radius=60.0, centers=GRID_CENTERS),
    "grid90": dict(radius=90.0, centers=GRID_CENTERS),
    "grid30": dict(radius=30.0, centers=GRID_CENTERS),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=160)
    ap.add_argument("--seed", type=int, default=20261012)
    ap.add_argument("--dps", type=int, default=120)
    ap.add_argument("--max-depth", type=int, default=15)
    ap.add_argument("--min-depth", type=int, default=1)
    ap.add_argument("--physical", type=int, default=16)
    ap.add_argument("--subdir", default="")
    ap.add_argument("--no-dump", action="store_true")
    args = ap.parse_args()
    global N_PHYSICAL, SCN_DIR, EXP_DIR
    N_PHYSICAL = args.physical
    if args.subdir:
        SCN_DIR = os.path.join(ROOT, "bench", args.subdir, "scenarios")
        EXP_DIR = os.path.join(ROOT, "bench", args.subdir, "expected")
    mp.mp.dps = args.dps
    os.makedirs(SCN_DIR, exist_ok=True)
    os.makedirs(EXP_DIR, exist_ok=True)
    rng = random.Random(args.seed)

    written = 0
    # 1. the user's dump
    dump_path = os.path.join(ROOT, "bench", "dumps", "dump_7_4_1_3_6_6_6.txt")
    if os.path.exists(dump_path) and not args.no_dump:
        scn = parse_dump(open(dump_path).read(), "dump_7_4_1_3_6_6_6")
        exp = expected_for(scn, to_mp_circles(scn))
        ok, worst = stable_under_precision(scn, to_mp_circles, exp)
        assert ok, f"oracle unstable on dump: {worst}"
        save(scn, os.path.join(SCN_DIR, scn["name"] + ".json"))
        save(exp, os.path.join(EXP_DIR, scn["name"] + ".json"))
        written += 1

    # 2. generated scenarios; depth spread weighted towards deep plans
    depths = list(range(args.min_depth, args.max_depth + 1))
    weights = [1 if d < 6 else 2 if d < 10 else 3 for d in depths]
    per_variant = {"grid60": 0.6, "grid90": 0.2, "grid30": 0.2}
    attempts = 0
    while written < args.count and attempts < args.count * 400:
        attempts += 1
        vname = rng.choices(list(per_variant), list(per_variant.values()))[0]
        v = VARIANTS[vname]
        circ_json = grid_circles(v["radius"], v["centers"])
        circles = to_mp_circles({"circles": circ_json})
        depth = rng.choices(depths, weights)[0]
        chain = rng.random() < 0.75
        player, cursor, plan = generate_one(rng, circles, depth, chain)
        name = f"{vname}_{'chain' if chain else 'mixed'}_d{depth:02d}_{written:03d}"
        scn = {
            "name": name,
            "player": [float(player.real), float(player.imag)],
            "cursor": [float(cursor.real), float(cursor.imag)],
            "circles": circ_json,
            "walls": True,
            "plan": [{"surface_id": a, "side": b} for a, b in plan],
            "tags": [vname, f"depth{depth}", "chain" if chain else "mixed"],
        }
        exp = expected_for(scn, circles)
        if exp is None:
            continue
        ok, worst = stable_under_precision(scn, to_mp_circles, exp)
        if not ok:
            print(f"skip {name}: oracle unstable ({worst})")
            continue
        save(scn, os.path.join(SCN_DIR, name + ".json"))
        save(exp, os.path.join(EXP_DIR, name + ".json"))
        written += 1
        print(f"{name}: plan={len(plan)} max_sens={exp['max_sens_to_cursor']:.3g}")
    print(f"wrote {written} scenarios in {attempts} attempts")


if __name__ == "__main__":
    main()
