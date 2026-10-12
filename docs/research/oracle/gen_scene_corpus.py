"""Oracle v2 corpus from the real levels (Stage 81+).

For every scene whose surfaces use only the effects the current stage supports,
generate random player / cursor / plan scenarios and record the oracle's
planned and physical traces (trace2 semantics), verified stable at double
precision.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_scene_corpus.py [--per-scene N] [--seed S]
Writes bench/scenes_corpus/{scenarios,expected}/<scene>__NN.json.
"""
import argparse
import glob
import json
import os
import random
import struct
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T                      # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
OUT = os.path.join(ROOT, "bench", "scenes_corpus")
SUPPORTED = {"pass", "terminal", "reflection", "inversion", "portal"}
LONG_PLAN_SCENES = ("two_mirrors", "three_mirrors", "parallel_mirrors")
D = 40


def f32(x):
    return struct.unpack("f", struct.pack("f", float(x)))[0]


def s(x):
    return mp.nstr(x, D, strip_zeros=False)


def scene_ok(surfs):
    for d in surfs:
        for side in ("left", "right"):
            c = d.get(side)
            if c is not None and c["type"] not in SUPPORTED:
                return False
    return True


def plannable(surfs):
    out = []
    for d in surfs:
        for side, key in (("LEFT", "left"), ("RIGHT", "right")):
            c = d.get(key)
            if c is not None and c["type"] in ("reflection", "inversion", "portal") and c.get("interactive"):
                out.append((int(d["id"]), side))
    return out


def ev_json(e):
    o = {"kind": e.kind, "surface_id": e.surface_id, "side": e.side, "applied": e.applied,
         "terminal": e.terminal, "on_segment": e.on_segment, "rehit": e.rehit}
    if e.point is not None:
        o["x"] = s(e.point.real)
        o["y"] = s(e.point.imag)
    return o


def expected(surfs_json, player, cursor, plan):
    sc = T.scene_from_json(surfs_json)
    pl = T.planned(sc, player, cursor, plan)
    ph = T.physical(sc, player, cursor, plan)
    return {"planned": [ev_json(e) for e in pl.events], "physical": [ev_json(e) for e in ph.events],
            "planned_targets": sorted(pl.targets), "physical_targets": sorted(ph.targets)}


def stable(surfs_json, player, cursor, plan, exp):
    old = mp.mp.dps
    mp.mp.dps = old * 2
    try:
        e2 = expected(surfs_json, player, cursor, plan)
    finally:
        mp.mp.dps = old
    for key in ("planned", "physical"):
        a, b = exp[key], e2[key]
        if len(a) != len(b):
            return False
        for x, y in zip(a, b):
            if (x["kind"], x["surface_id"], x["side"], x["applied"]) != (y["kind"], y["surface_id"], y["side"], y["applied"]):
                return False
            if "x" in x and (abs(mp.mpf(x["x"]) - mp.mpf(y["x"])) > mp.mpf(10) ** -30
                             or abs(mp.mpf(x["y"]) - mp.mpf(y["y"])) > mp.mpf(10) ** -30):
                return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-scene", type=int, default=12)
    ap.add_argument("--seed", type=int, default=81)
    ap.add_argument("--dps", type=int, default=120)
    args = ap.parse_args()
    mp.mp.dps = args.dps
    rng = random.Random(args.seed)
    for sub in ("scenarios", "expected"):
        os.makedirs(os.path.join(OUT, sub), exist_ok=True)
    total = 0
    for dump in sorted(glob.glob(os.path.join(ROOT, "bench", "scenes", "*.surfaces.json"))):
        dj = json.load(open(dump))
        surfs = dj["surfaces"]
        name = os.path.basename(dump).replace(".surfaces.json", "")
        if not surfs or not scene_ok(surfs):
            continue
        pl_choices = plannable(surfs)
        rng = random.Random(f"{args.seed}:{name}")     # per scene: adding scenes changes nothing else
        made = 0
        attempts = 0
        while made < args.per_scene and attempts < args.per_scene * 20:
            attempts += 1
            player = [f32(rng.uniform(570, 1350)), f32(rng.uniform(250, 830))]
            cursor = [f32(rng.uniform(570, 1350)), f32(rng.uniform(250, 830))]
            depth_max = 10 if name.endswith(LONG_PLAN_SCENES) else 4
            depth = 0 if not pl_choices else rng.randint(0, depth_max)
            plan = []
            while len(plan) < depth:
                e = rng.choice(pl_choices)
                if plan and plan[-1][0] == e[0] and rng.random() < 0.7:
                    continue
                plan.append(e)
            exp = expected(surfs, player, cursor, plan)
            if not stable(surfs, player, cursor, plan, exp):
                print(f"skip unstable {name}")
                continue
            sid = f"{name}__{made:02d}"
            scn = {"name": sid, "scene": dj["scene"], "surfaces": surfs, "player": player, "cursor": cursor,
                   "plan": [{"surface_id": a, "side": b} for a, b in plan]}
            json.dump(scn, open(os.path.join(OUT, "scenarios", sid + ".json"), "w"), indent=1)
            exp["name"] = sid
            exp["dps"] = mp.mp.dps
            json.dump(exp, open(os.path.join(OUT, "expected", sid + ".json"), "w"), indent=1)
            made += 1
        total += made
        print(f"{name}: {made}")
    print("total", total)


if __name__ == "__main__":
    main()
