"""Stage 86 fixtures: ghost mode (the reserved "ghost" flag), traced by the
oracle (trace2). Same format as gen_fixtures_stage85.py.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage86.py
Writes bench/fixtures/stage86_ghost.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_fixtures_stage81 import INV, REFL, TERM, line, room  # noqa: E402
from gen_fixtures_stage85 import circle, fixture, kinds  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

GHOST = {"type": "pass", "interactive": True, "state": {"key": "ghost", "op": "toggle", "value": True}}
PROJ = {"type": "line_normal_projection", "interactive": True, "back": False}


def ghost_line(sid, a, b):
    return line(sid, a, b, GHOST, GHOST)


def ghosted(f):
    return [bool((e["flags"] or {}).get("ghost")) for e in f["expected"]]


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(10, -1000, -1000, 1000, 1000)
    # one toggler: ghost through the right wall, wrap through infinity, back in
    # through the left wall, materialize at the same toggler, stop at the wall
    tog = ghost_line(1, (300, -400), (300, 400))
    f = fixture("ghost_loops_through_infinity", [tog] + walls, (100.0, 0.0), direction=(1.0, 0.0625))
    ks = kinds(f)
    assert ks[0][1] == 1 and ks[0][4] and ks[-1][3] and ks[-1][1] == 11, ks
    assert sum(1 for k in ks if k[1] == 1 and k[4]) == 2, ks
    fx.append(f)
    # everything passes while ghosted: a mirror, an inversion circle, a block,
    # a projective; the second toggler materializes, the mirror after it acts
    scene = [ghost_line(1, (200, -400), (200, 400)),
             line(2, (300, -400), (300, 400), REFL, REFL),
             circle(3, (500.0, 0.0), 80.0, INV, INV),
             line(4, (700, -400), (700, 400), TERM, TERM),
             line(5, (800, -400), (800, 400), PROJ, PROJ),
             ghost_line(6, (900, -400), (900, 400)),
             line(7, (950, -400), (950, 400), REFL, REFL)] + walls
    f = fixture("ghost_passes_everything", scene, (100.0, 0.0), direction=(1.0, 0.0625))
    ks = kinds(f)
    i6 = next(i for i, k in enumerate(ks) if k[1] == 6)
    assert all(not k[3] for k in ks[1:i6]), ks
    assert ks[i6 + 1][1] == 7 and ks[i6 + 1][3], ks
    fx.append(f)
    # two togglers: ghost from x = 300 to x = 600; the mirror between passes,
    # the mirror after reflects
    two = [ghost_line(1, (300, -400), (300, 400)), line(2, (450, -400), (450, 400), REFL, REFL),
           ghost_line(3, (600, -400), (600, 400)), line(4, (800, -400), (800, 400), REFL, REFL)] + walls
    f = fixture("ghost_ends_at_next_toggler", two, (100.0, 0.0), direction=(1.0, 0.0625))
    ks = kinds(f)
    # (Stage 134: while ghosted the mirror 2 is passed -- no candidate, never computed)
    assert [k[1] for k in ks[:3]] == [1, 3, 4] and ks[2][3], ks
    fx.append(f)
    # planned through the ghost stretch: the plan names both togglers and the
    # mirror after them; planned and physical agree through the cursor
    for mode in ("planned", "physical"):
        f = fixture("ghost_planned_%s" % mode, two, (100.0, 0.0), plan=[(1, "RIGHT"), (3, "RIGHT"), (4, "RIGHT")],
                    mode=mode, cursor=(700.0, 150.0))
        assert any(e["kind"] == "cursor" for e in f["expected"]), kinds(f)
        fx.append(f)
    # a plan entry inside the ghost stretch resolves to a pass: consumed
    # without a frame change (the aim goes straight at the cursor)
    f = fixture("ghost_plan_entry_inside_stretch", two, (100.0, 0.0), plan=[(1, "RIGHT"), (2, "RIGHT")],
                mode="planned", cursor=(500.0, 30.0))
    assert not any(e["applied"] for e in f["expected"] if e["surface_id"] == 2), kinds(f)
    fx.append(f)
    out = os.path.join(ROOT, "bench", "fixtures", "stage86_ghost.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], kinds(f)[:8], len(f["expected"]), ghosted(f)[:8])


if __name__ == "__main__":
    main()
