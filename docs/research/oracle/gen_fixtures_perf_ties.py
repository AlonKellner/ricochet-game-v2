"""Perf P2b fixtures: exact concurrency on a circular beam (GAME_SPEC 9.3).

On an exact line, adjacent crossings of two line carriers that meet the line
at one point share one parameter and the id orders them (_concurrent_ties).
The same holds on a circular beam whose exact circle passes the carriers'
common point (_concurrent_ties_gc): the beam's exact generalized circle is
carried through the maps as the exact line is.

The scene: an inversion circle centred at (500, 500), radius 100. A straight
shot along y = 450 becomes, after the inversion, the circle centred at
(500, 400), radius 100, which passes X = (600, 400) exactly. Two pass lines
cross there: y = 1000 - x and y = x / 2 + 100 (each segment meets the circle
at X only). The hits at X come in id order, whichever id each line has; a
control with one line moved by 1/16 px keeps the geometric order.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_perf_ties.py
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

    # the lower id first, both ways round
    case("circle_beam_tie_falling_low", [inv, line(2, *FALLING, PASS, PASS), line(3, *RISING, PASS, PASS)] + walls, [2, 3])
    case("circle_beam_tie_rising_low", [inv, line(2, *RISING, PASS, PASS), line(3, *FALLING, PASS, PASS)] + walls, [2, 3])
    # control: the falling line moved by 1/16 px along x (no common point on the
    # beam): one hit at X, the other a little apart, in walk order
    moved = ((550.0625, 450), (650.0625, 350))
    f = fixture("circle_beam_no_tie_control", [inv, line(2, *moved, PASS, PASS), line(3, *RISING, PASS, PASS)] + walls,
                PLAYER, direction=DIRECTION)
    assert hits_near(f, 600.0, 400.0) == [3], [(e["kind"], e["surface_id"], e.get("x"), e.get("y")) for e in f["expected"]]
    fx.append(f)

    out = os.path.join(ROOT, "bench", "fixtures", "perf_circle_ties.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(e["kind"], e["surface_id"], e["applied"]) for e in f["expected"]][:8])


if __name__ == "__main__":
    main()
