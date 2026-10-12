"""Regenerate the precision benchmark corpora under oracle v2 semantics (Stage 81).

The v1 corpora (gen_corpus.py, beam.py) traced full inversion circles only:
screen walls were not events and a beam's loop ended at its departure point.
v2 (trace2.py) is the game's full rule set: walls are pass events, the
departure point is re-hit as an ordinary hit, MAX_HITS = 32 counts every
event. Each scenario gains "surfaces" (scene defs, BeamScene format) built
exactly as the game builds them (PrecisionBench.build_surfaces /
level_settings: GeneralizedCircle.from_circle carriers, ids in creation
order, walls as pass surfaces after the circles); expected traces are
recomputed with adaptive precision, accepted when stable at twice the digits.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/regen_bench_v2.py [corpus ...]
corpus: "" (bench/) and/or "stress" (bench/stress/); default both.
"""
import glob
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T                                          # noqa: E402
from tscn_import import full_circle_carrier, full_circle_points  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
WALLS = [(0.0, 0.0, 1920.0, 0.0), (1920.0, 0.0, 1920.0, 1080.0), (1920.0, 1080.0, 0.0, 1080.0), (0.0, 1080.0, 0.0, 0.0)]
D = 40
DPS_LADDER = [120, 240, 480, 960, 1920]


def s(x):
    return mp.nstr(x, D, strip_zeros=False)


def defs_for(scn):
    out = []
    inv = {"interactive": True, "type": "inversion"}
    for c in scn["circles"]:
        car = full_circle_carrier(c["center"][0], c["center"][1], c["radius"])
        st, en, via = full_circle_points(c["center"][0], c["center"][1], c["radius"])
        out.append({"circle": car, "id": int(c["id"]), "start": list(st), "end": list(en), "via": list(via),
                    "full": True, "line": False, "left": dict(inv), "right": dict(inv),
                    "is_target": False, "player_solid": True})
    if scn.get("walls", True):
        nid = max(int(c["id"]) for c in scn["circles"]) + 1
        for x0, y0, x1, y1 in WALLS:
            pas = {"interactive": False, "type": "pass"}
            out.append({"circle": None, "id": nid, "start": [x0, y0], "end": [x1, y1],
                        "via": [(x0 + x1) / 2.0, (y0 + y1) / 2.0], "full": False, "line": True,
                        "left": dict(pas), "right": dict(pas), "is_target": False, "player_solid": False})
            nid += 1
    return out


def ev_json(e):
    o = {"kind": e.kind, "surface_id": e.surface_id, "side": e.side, "applied": e.applied,
         "terminal": e.terminal, "on_segment": e.on_segment, "rehit": e.rehit}
    if e.point is not None:
        o["x"] = s(e.point.real)
        o["y"] = s(e.point.imag)
    return o


def trace_both(surfs, player, cursor, plan):
    sc = T.scene_from_json(surfs)
    return {"planned": [ev_json(e) for e in T.planned(sc, player, cursor, plan).events],
            "physical": [ev_json(e) for e in T.physical(sc, player, cursor, plan).events]}


def same(a, b):
    for key in ("planned", "physical"):
        if len(a[key]) != len(b[key]):
            return False
        for x, y in zip(a[key], b[key]):
            if (x["kind"], x["surface_id"], x["side"], x["applied"], x["rehit"]) != \
                    (y["kind"], y["surface_id"], y["side"], y["applied"], y["rehit"]):
                return False
            if "x" in x and (abs(mp.mpf(x["x"]) - mp.mpf(y["x"])) > mp.mpf(10) ** -30
                             or abs(mp.mpf(x["y"]) - mp.mpf(y["y"])) > mp.mpf(10) ** -30):
                return False
    return True


def regen(corpus):
    base = os.path.join(ROOT, "bench", corpus) if corpus else os.path.join(ROOT, "bench")
    unstable = []
    files = sorted(glob.glob(os.path.join(base, "scenarios", "*.json")))
    for fn in files:
        scn = json.load(open(fn))
        name = scn["name"]
        surfs = defs_for(scn)
        plan = [(int(e["surface_id"]), e["side"]) for e in scn["plan"]]
        exp = None
        dps_ok = None
        for dps in DPS_LADDER:
            mp.mp.dps = dps
            a = trace_both(surfs, scn["player"], scn["cursor"], plan)
            mp.mp.dps = dps * 2
            b = trace_both(surfs, scn["player"], scn["cursor"], plan)
            if same(a, b):
                exp, dps_ok = a, dps
                break
        if exp is None:
            unstable.append(name)
            exp, dps_ok = b, DPS_LADDER[-1] * 2
        scn["surfaces"] = surfs
        json.dump(scn, open(fn, "w"), indent=1)
        exp.update({"name": name, "dps": dps_ok, "semantics": "v2"})
        json.dump(exp, open(os.path.join(base, "expected", name + ".json"), "w"), indent=1)
        print(f"{corpus or 'bench'}/{name}: dps {dps_ok}, planned {len(exp['planned'])}, physical {len(exp['physical'])}",
              flush=True)
    print(f"{corpus or 'bench'}: {len(files)} scenarios, unstable at {DPS_LADDER[-1]} dps: {unstable}")


def main():
    corpora = sys.argv[1:] if len(sys.argv) > 1 else ["", "stress"]
    for c in corpora:
        regen(c)


if __name__ == "__main__":
    main()
