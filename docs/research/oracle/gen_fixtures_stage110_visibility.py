"""Stage 110: cursors labelled visible or not by the oracle's definition of the
visibility region (visibility.py), on shipped levels' scene dumps.

Each case: a level, a player, a plan; its cursors: a grid over the level, every
segment endpoint, and the points beyond each endpoint on the line from the
player (2e - P: exactly on a shadow's edge).

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage110_visibility.py
Writes bench/fixtures/stage110_visibility.json.
"""
import json
from fractions import Fraction
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
import visibility as V  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TRANSFORMATIVE = ("reflection", "inversion", "portal")


def dump(name):
    return json.load(open(os.path.join(ROOT, "bench", "scenes", name + ".surfaces.json")))["surfaces"]


def sides(surfs, types):
    """(id, side) of every side whose effect type is in types."""
    out = []
    for s in surfs:
        if s.get("pre_transform"):
            continue
        for side, key in (("LEFT", "left"), ("RIGHT", "right")):
            cfg = s.get(key)
            if cfg and cfg.get("type") in types:
                out.append((s["id"], side))
    return out


def carrier_extension(surfs, plan):
    """Cursors on the planned surfaces' carriers (lines, and a portal's pair)
    beyond their segments: S + k (E - S), k in {-1, -1/2, -1/8, 9/8, 3/2, 2}, kept when
    exact in float32 (where a walk departs from a planned surface crossed off
    its segment -- B4)."""
    by_id = {s["id"]: s for s in surfs}
    ids = []
    for sid, _side in plan:
        s = by_id.get(sid)
        if s is None:
            continue
        ids.append(sid)
        for key in ("left", "right"):
            cfg = s.get(key) or {}
            if cfg.get("type") == "portal" and cfg.get("pair") is not None:
                ids.append(cfg["pair"])
    out = []
    for sid in ids:
        s = by_id[sid]
        if not s.get("line", True) or s.get("full"):
            continue
        S, E = (Fraction(s["start"][0]), Fraction(s["start"][1])), (Fraction(s["end"][0]), Fraction(s["end"][1]))
        for k in (Fraction(-1), Fraction(-1, 2), Fraction(-1, 8), Fraction(9, 8), Fraction(3, 2), Fraction(2)):
            p = (S[0] + k * (E[0] - S[0]), S[1] + k * (E[1] - S[1]))
            if all(Fraction(V._f32(c)) == c for c in p):
                out.append((float(p[0]), float(p[1])))
    return out


def cursors(surfs, player, plan=()):
    pts = carrier_extension(surfs, plan)
    for i in range(14):
        for j in range(8):
            pts.append((40.0 + i * 140.0, 40.0 + j * 140.0))
    for s in surfs:
        if s.get("full"):
            continue
        for e in (s["start"], s["end"]):
            pts.append((float(e[0]), float(e[1])))
            pts.append((2.0 * e[0] - player[0], 2.0 * e[1] - player[1]))
    out = []
    for p in pts:
        if -50 <= p[0] <= 1970 and -50 <= p[1] <= 1130 and p not in out and p != player:
            out.append(p)
    return out


CANDIDATES = [(760.0, 400.0), (1200.0, 400.0), (950.0, 700.0), (650.0, 650.0), (1250.0, 700.0)]


def planned_case(name, scene, plan):
    """The case at the first candidate player from which the plan shows some cursors."""
    best = None
    for player in CANDIDATES:
        c = case(name, scene, player, plan)
        n = sum(1 for x in c["cursors"] if x[2])
        if best is None or n > best[0]:
            best = (n, c)
        if n >= 10:
            break
    return best[1]


def case(name, scene, player, plan):
    surfs = dump(scene)
    sc = T.scene_from_json(surfs)
    labelled = []
    for c in cursors(surfs, player, plan):
        v = V.visible(sc, player, c, plan)
        if v is not None:
            labelled.append([c[0], c[1], bool(v)])
    return {"name": name, "scene": scene, "player": list(player),
            "plan": [{"surface_id": a, "side": b} for a, b in plan], "cursors": labelled}


def main():
    mp.mp.dps = 50
    out = []
    room = (760.0, 400.0)   # inside the test levels' room (560..1360 x 240..840)
    out.append(case("room_with_mirror_no_plan", "test_levels__room_with_mirror", room, []))
    mirror = sides(dump("test_levels__room_with_mirror"), ("reflection",))[0]
    out.append(planned_case("room_with_mirror_planned", "test_levels__room_with_mirror", [mirror]))
    two = sides(dump("test_levels__two_mirrors"), ("reflection",))
    out.append(planned_case("two_mirrors_one", "test_levels__two_mirrors", [two[0]]))
    out.append(planned_case("two_mirrors_both", "test_levels__two_mirrors", [two[0], two[-1]]))
    inv = sides(dump("test_levels__room_with_inversion"), ("inversion", "reflection"))
    out.append(planned_case("room_with_inversion_planned", "test_levels__room_with_inversion", [inv[0]]))
    out.append(case("pre_inverted_no_plan", "playground__pre_inverted", (500.0, 300.0), []))
    out.append(case("empty_room_infinity", "test_levels__empty_room", (500.0, 300.0), [(T.INFINITY_ID, "LEFT")]))
    out.append(case("room_with_passthroughs_no_plan", "test_levels__room_with_passthroughs", room, []))
    # B5: a co-carrier write of the planned value (hit_effects: 15 fires, 16 is planned)
    out.append(case("hit_effects_state_plan", "test_levels__hit_effects", (960.0, 540.0), [(16, "RIGHT")]))
    # B4: the reported thin lines -- departures off the planned segment (float32 player)
    three = (V._f32(914.4775), V._f32(425.2639))
    out.append(case("three_mirrors_departures", "test_levels__three_mirrors", three, [(4, "LEFT"), (6, "RIGHT")]))
    path = os.path.join(ROOT, "bench", "fixtures", "stage110_visibility.json")
    json.dump({"dps": mp.mp.dps, "cases": out}, open(path, "w"), indent=1)
    for c in out:
        print(c["name"], len(c["cursors"]), "cursors,", sum(1 for x in c["cursors"] if x[2]), "visible")


if __name__ == "__main__":
    main()
