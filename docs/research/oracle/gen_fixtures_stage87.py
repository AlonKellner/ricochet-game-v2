"""Stage 87 fixtures: plannable projectives (GAME_SPEC 13.3 / 13.4), traced by
the oracle (trace2). Same format as gen_fixtures_stage85.py.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage87.py
Writes bench/fixtures/stage87_projective_plans.json.
"""
import itertools
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_fixtures_stage81 import REFL, line, room  # noqa: E402
from gen_fixtures_stage85 import circle, fixture, kinds  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def proj(kind, back=False, normal=None):
    c = {"type": kind, "interactive": True, "back": back}
    if normal is not None:
        c["normal"] = list(normal)
    return c


def reaches(f):
    return any(e["kind"] == "cursor" for e in f["expected"])


def planned_pair(fx, name, surfs, player, cursor, ids, sides_only=None):
    """Planned and physical fixtures of a plan over `ids`, with the plan sides
    chosen as the first combination whose planned trace reaches the cursor
    (or exactly `sides_only`)."""
    combos = [sides_only] if sides_only else itertools.product(("LEFT", "RIGHT"), repeat=len(ids))
    for sides in combos:
        plan = list(zip(ids, sides))
        f = fixture(name + "_planned", surfs, player, plan=plan, mode="planned", cursor=cursor)
        if reaches(f):
            fx.append(f)
            fx.append(fixture(name + "_physical", surfs, player, plan=plan, mode="physical", cursor=cursor))
            return f
    raise AssertionError("%s: no side combination reaches the cursor" % name)


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(10, -1000, -1000, 1000, 1000)
    # line normal (through / back): H = the foot of the cursor
    ln = lambda back: line(1, (300, -400), (300, 400), *(proj("line_normal_projection", back),) * 2)  # noqa: E731
    planned_pair(fx, "line_normal_through", [ln(False)] + walls, (100.0, -100.0), (600.0, 200.0), [1])
    planned_pair(fx, "line_normal_back", [ln(True)] + walls, (100.0, -100.0), (100.0, 300.0), [1])
    # circle normal: c +- R u, the one whose exit reaches the cursor directly
    cn = lambda back: circle(1, (500.0, 0.0), 200.0, *(proj("circle_normal_projection", back),) * 2)  # noqa: E731
    planned_pair(fx, "circle_normal_through", [cn(False)] + walls, (100.0, 0.0), (900.0, 100.0), [1])
    planned_pair(fx, "circle_normal_back", [cn(True)] + walls, (100.0, 0.0), (0.0, -150.0), [1])
    # directional: the line through the cursor along the normal
    ld = line(1, (300, -400), (300, 400), *(proj("line_directional_projection", False, (1, 1)),) * 2)
    planned_pair(fx, "line_directional", [ld] + walls, (100.0, -100.0), (600.0, 250.0), [1])
    cd = circle(1, (500.0, 0.0), 100.0, *(proj("circle_directional_projection", False, (1, 0)),) * 2)
    planned_pair(fx, "circle_directional", [cd] + walls, (200.0, 30.0), (800.0, 60.0), [1])
    # a full circle acts at both crossings: the realizable plan names both
    f = planned_pair(fx, "circle_normal_both_crossings", [cn(False)] + walls, (100.0, 0.0), (900.0, 100.0), [1, 1], ("LEFT", "RIGHT"))
    f = planned_pair(fx, "circle_directional_both_crossings", [cd] + walls, (200.0, 30.0), (800.0, 60.0), [1, 1], ("LEFT", "RIGHT"))
    # mixed chain: mirror, line normal, mirror
    mixed = [line(1, (400, -500), (400, 200), REFL, REFL),
             line(2, (0, 200), (380, 200), *(proj("line_normal_projection", False),) * 2),
             line(3, (0, 450), (380, 450), REFL, REFL)] + walls
    planned_pair(fx, "mixed_chain", mixed, (200.0, -200.0), (250.0, 300.0), [1, 2, 3])
    # user capture (playground/projective): a 7-entry projective plan; the
    # second cursor needs the on-arc candidate at the circle normal (entry 5)
    # -- the nearer one dead-ends at entry 3: backtracking finds the exact path
    pl = json.load(open(os.path.join(ROOT, "bench", "scenes", "playground__projective.surfaces.json")))["surfaces"]
    uplan = [(7, "RIGHT"), (10, "RIGHT"), (8, "RIGHT"), (10, "LEFT"), (7, "LEFT"), (9, "LEFT"), (13, "LEFT")]
    for k, cur in enumerate([(383.3994, 487.9036), (146.1522, 559.0353)]):
        for mode in ("planned", "physical"):
            f = fixture("user_capture_%d_%s" % (k, mode), pl, (842.1705, 878.0266), plan=uplan, mode=mode, cursor=cur)
            assert reaches(f), kinds(f)
            fx.append(f)
    out = os.path.join(ROOT, "bench", "fixtures", "stage87_projective_plans.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(p["surface_id"], p["side"]) for p in f["plan"]], kinds(f)[:6], reaches(f))


if __name__ == "__main__":
    main()
