"""Stage 201 fixtures: the cursor before a planned terminal (GAME_SPEC 13.5, 12.6, the user).

Usage:
  docs/research/.venv/bin/python -I docs/research/oracle/gen_fixtures_stage201.py

Writes bench/fixtures/stage201_terminal_cursor.json: the cursor counts once the
plan's entries are consumed, or all but a planned terminal, reached or not. Past
the terminal it is not reached. And the cursor counts toward neither event limit:
after all ten significant events it is still reached.
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
P = (0.0, 0.0)


def kinds(f):
    return [e["kind"] for e in f["expected"]]


def main():
    mp.mp.dps = 80
    fx = []
    wall = line(9, (400, -500), (400, 500), TERM, TERM)
    term = line(1, (200, -20), (200, 20), TERM, TERM)
    ring = circle(1, (200, 60), 16, TERM, TERM)
    mirror = line(2, (100, -100), (100, 100), REFL, REFL)
    far = line(3, (-200, -500), (-200, 500), TERM, TERM)
    side = T.scene_from_json([term])
    left = T.trace_physical_direction(side, P, mp.mpc(1, 0)).events[0].side
    for mode in ("planned", "physical"):
        walks = [
            # on the way to it: the cursor, then the terminal
            ("on_the_way", [term, wall], [(1, left)], (100.0, 0.0), ["cursor", "hit"]),
            # the aim misses a line terminal: the cursor, then what the walk meets
            ("line_missed", [term, wall], [(1, left)], (100.0, 40.0), None),
            # the aim misses a circle terminal
            ("circle_missed", [ring, wall], [(1, left)], (100.0, 0.0), None),
            # the other way from it: no aim away from the cursor
            ("the_other_way", [term, far], [(1, left)], (-50.0, 0.0), ["cursor", "hit"]),
            # past it: not reached
            ("past_it", [term, wall], [(1, left)], (300.0, 0.0), ["hit"]),
        ]
        for name, surfs, plan, cursor, want in walks:
            f = fixture(name + "_" + mode, surfs, P, plan=plan, mode=mode, cursor=cursor)
            if want is not None:
                assert kinds(f) == want, (name, mode, kinds(f))
            else:
                assert kinds(f)[0] == "cursor", (name, mode, kinds(f))
            fx.append(f)
        # after a mirror: its entry first, then the cursor before the terminal behind it
        back = line(1, (-100, -500), (-100, 500), TERM, TERM)
        first = T.trace_physical_direction(T.scene_from_json([mirror, back]), P, mp.mpc(1, 0.1)).events
        plan = [(2, first[0].side), (1, first[1].side)]
        f = fixture("after_a_mirror_" + mode, [mirror, back], P, plan=plan, mode=mode, cursor=(50.0, 5.0))
        assert kinds(f) == ["hit", "cursor", "hit"], (mode, kinds(f))
        fx.append(f)
    # the cursor counts toward neither limit: after the ten significant events it is reached
    a, b = line(1, (100, -5000), (100, 5000), REFL, REFL), line(2, (-100, -5000), (-100, 5000), REFL, REFL)
    hits = T.trace_physical_direction(T.scene_from_json([a, b]), P, mp.mpc(1, 0.01)).events[:T.MAX_SIGNIFICANT]
    plan = [(e.surface_id, e.side) for e in hits]
    assert len(plan) == T.MAX_SIGNIFICANT
    f = fixture("cursor_after_ten_significant", [a, b], P, plan=plan, mode="planned", cursor=(-50.0, 5.0))
    k = kinds(f)
    assert k.count("hit") == T.MAX_SIGNIFICANT and "cursor" in k and k.index("cursor") == T.MAX_SIGNIFICANT, k
    fx.append(f)
    path = os.path.join(ROOT, "bench", "fixtures", "stage201_terminal_cursor.json")
    with open(path, "w") as out:
        json.dump({"dps": mp.mp.dps, "fixtures": fx}, out, indent=1)
        out.write("\n")
    print(path, len(fx), "fixtures")


if __name__ == "__main__":
    main()
