"""Perf P2b fixtures: exact concurrency on a circular beam (GAME_SPEC 9.3).

On an exact line, adjacent crossings of two line carriers that meet the line
at one point share one parameter and the id orders them (_concurrent_ties).
The same holds on a circular beam whose exact circle passes the carriers'
common point (_concurrent_ties_gc): the beam's exact generalized circle is
carried through the maps as the exact line is. (Stage 138: the two crossings at
X are two surfaces with a consequence at one point -- the arrow stops there.)

The scene: an inversion circle centred at (500, 500), radius 100. A straight
shot along y = 450 becomes, after the inversion, the circle centred at
(500, 400), radius 100, which passes X = (600, 400) exactly. Two pass lines
cross there: y = 1000 - x and y = x / 2 + 100 (each segment meets the circle
at X only). The hits at X come in id order, whichever id each line has; a
control with one line moved by 1/16 px keeps the geometric order.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_perf_ties.py
K9 and K7 (docs/TDD_14_VISIBILITY.md): the same rules in a pre-transform level.

Writes bench/fixtures/perf_circle_ties.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import INV, PASS, line, room  # noqa: E402
from gen_fixtures_stage85 import circle, fixture  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

PLAYER = (200.0, 450.0)
DIRECTION = (1.0, 0.0)
FALLING = ((550, 450), (650, 350))   # y = 1000 - x, through X
# Stage 134: lines that pass the beam and write a flag -- candidates (a pure pass
# line is never computed)
W2 = dict(PASS, interactive=True, states=[{"key": "a", "op": "toggle", "value": True}])
W3 = dict(PASS, interactive=True, states=[{"key": "b", "op": "toggle", "value": True}])
RISING = ((500, 350), (700, 450))    # y = x / 2 + 100, through X


def hits_near(f, x, y, tol=1e-6):
    out = []
    for e in f["expected"]:
        if e["kind"] == "hit" and "x" in e:
            px, py = float(e["x"]), float(e["y"])
            if abs(px - x) < tol and abs(py - y) < tol:
                out.append(e["surface_id"])
    return out


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(10, -1000, -1000, 2000, 2000)
    inv = circle(1, (500, 500), 100, INV, INV)

    def case(name, surfs, expect_at_x):
        f = fixture(name, surfs, PLAYER, direction=DIRECTION)
        at_x = hits_near(f, 600.0, 400.0)
        assert at_x == expect_at_x, (name, at_x, [(e["kind"], e["surface_id"]) for e in f["expected"]])
        fx.append(f)

    # the tie is exact both ways round -- Stage 138 (the user): two surfaces with a
    # consequence hold X, so the arrow stops there, the event the lowest id's
    case("circle_beam_tie_falling_low", [inv, line(2, *FALLING, W2, W2), line(3, *RISING, W3, W3)] + walls, [2])
    case("circle_beam_tie_rising_low", [inv, line(2, *RISING, W2, W2), line(3, *FALLING, W3, W3)] + walls, [2])
    # control: the falling line moved by 1/16 px along x (no common point on the
    # beam): one hit at X, the other a little apart, in walk order
    moved = ((550.0625, 450), (650.0625, 350))
    f = fixture("circle_beam_no_tie_control", [inv, line(2, *moved, W2, W2), line(3, *RISING, W3, W3)] + walls,
                PLAYER, direction=DIRECTION)
    assert hits_near(f, 600.0, 400.0) == [3], [(e["kind"], e["surface_id"], e.get("x"), e.get("y")) for e in f["expected"]]
    fx.append(f)

    # K9: a pre-transform level (playground/pre_inverted), plan [2 L, 3 L, 4 R],
    # the cursor on surface 4's carrier: its image X = (0, 880) is the corner of
    # surface 4's and surface 8's carriers, which the start circle (through the
    # player, X and Q0: exact) passes. Both hits there in id order.
    d = json.load(open(os.path.join(ROOT, "bench", "scenes", "playground__pre_inverted.surfaces.json")))
    plan = [(2, T.LEFT), (3, T.LEFT), (4, T.RIGHT)]
    for player, modes in (((760.0, 640.0), ("physical", "planned")), ((1350.0, 443.33334), ("physical",))):
        for mode in modes:
            f = fixture("k9_pre_inverted_%d_%s" % (int(player[0]), mode), d["surfaces"], player, plan=plan, mode=mode, cursor=(1900.0, 880.0))
            if mode == "physical":
                # (Stage 131: no loop detection -- the loop repeats, the pair each time)
                # (Stage 134: surface 8 passes on both sides -- no candidate: 4's hits alone)
                h = hits_near(f, 0.0, 880.0)
                assert h and all(x == 4 for x in h), (f["name"], h)
            fx.append(f)

    # K7: the same shot from (1350, 443.33334): the planned walk consumes surface 4
    # exactly at the cursor (the start circle's image passes it), so the cursor is
    # the departure point after it: reached at once -- at every precision (before,
    # at 80 digits at once, at 60 after the lap)
    for mode in ("planned", "physical"):
        f = fixture("k7_pre_inverted_1350_%s" % mode, d["surfaces"], (1350.0, 443.33334), plan=plan, mode=mode, cursor=(1900.0, 880.0))
        if mode == "planned":
            ks = [(e["kind"], e["surface_id"]) for e in f["expected"]]
            ci = ks.index(("cursor", -1))
            assert ks[ci - 1] == ("hit", 4), ks[:ci + 1]
            old_dps = mp.mp.dps
            for dps in (40, 120):
                mp.mp.dps = dps
                g = fixture("probe", d["surfaces"], (1350.0, 443.33334), plan=plan, mode=mode, cursor=(1900.0, 880.0))
                assert [(e["kind"], e["surface_id"]) for e in g["expected"]] == ks, dps
            mp.mp.dps = old_dps
        fx.append(f)

    out = os.path.join(ROOT, "bench", "fixtures", "perf_circle_ties.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(e["kind"], e["surface_id"], e["applied"]) for e in f["expected"]][:8])


if __name__ == "__main__":
    main()
