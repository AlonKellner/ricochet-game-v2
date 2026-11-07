"""Stage 199 fixtures: the level's infinity_loop (GAME_SPEC 12.6, the user).

Usage:
  docs/research/.venv/bin/python -I docs/research/oracle/gen_fixtures_stage199.py

Writes bench/fixtures/stage199_infinity_loop.json: every walk twice. With
infinity_loop true it is the walk without the option, event for event; false,
it is cut at the frame's infinity: a "max" event there, nothing past it.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import REFL, TERM, line  # noqa: E402
from gen_fixtures_stage85 import circle, fixture  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
INV = {"type": "inversion", "interactive": True}
INF = T.INFINITY_ID


def kinds(f):
    return [e["kind"] for e in f["expected"]]


def main():
    mp.mp.dps = 80
    east = (1.0, 0.0)
    off_ray = line(1, (100, 10), (100, 500), REFL, REFL)
    behind = line(1, (-100, -500), (-100, 500), REFL, REFL)
    ahead = line(1, (100, -500), (100, 500), REFL, REFL)
    missed = circle(1, (500, 300), 16, TERM, TERM)
    ring = circle(1, (200, 30), 100, INV, INV)
    cases = [
        # name, surfaces, walk, the kinds with the option off
        ("nothing_on_the_beam", [off_ray], dict(direction=east), ["max"]),
        # (Stage 206: the missed terminal's counterpart ends the planned walk, at the tangent length)
        ("planned_entry_the_aim_misses", [missed], dict(plan=[(1, "LEFT")], mode="planned", direction=east), ["counterpart"]),
        ("a_mirror_past_infinity", [behind], dict(direction=east), ["max"]),
        ("reflected_first", [ahead], dict(direction=east), ["hit", "max"]),
        ("planned_infinity_aimed_past", [off_ray], dict(plan=[(INF, "")], mode="planned", cursor=(-50.0, 0.0)), ["max"]),
        ("an_entry_past_planned_infinity", [behind], dict(plan=[(INF, ""), (1, "RIGHT")], mode="planned", cursor=(-50.0, 0.0)), ["max"]),
        ("an_inversion_and_its_pop_first", [ring], dict(direction=east), ["hit", "hit", "max"]),
    ]
    fx = []
    for name, surfs, walk, off in cases:
        plain = fixture(name, surfs, (0.0, 0.0), **walk)
        on = fixture(name + "_on", surfs, (0.0, 0.0), infinity_loop=True, **walk)
        assert on["expected"] == plain["expected"], name
        f = fixture(name + "_off", surfs, (0.0, 0.0), infinity_loop=False, **walk)
        assert kinds(f) == off, (name, kinds(f))
        assert kinds(on) != kinds(f) or kinds(f) == ["counterpart"], name   # (Stage 206: a terminal's counterpart ends both)
        assert "infinity" not in kinds(f) and (f["expected"][-1]["surface_id"] == INF or kinds(f) == ["counterpart"]), name
        fx += [on, f]
    path = os.path.join(ROOT, "bench", "fixtures", "stage199_infinity_loop.json")
    with open(path, "w") as out:
        json.dump({"dps": mp.mp.dps, "fixtures": fx}, out, indent=1)
        out.write("\n")
    print(path, len(fx), "fixtures")


if __name__ == "__main__":
    main()
