"""Stage 112 fixtures (K10): an exact line never meets a parallel carrier.

The planned chain after a projective leaves along an exact line (the normal
through the cursor's image, the radial through the centre, the given
direction). A tier's rounded beam carries a tilt of an ulp, and before the
wrap through infinity it would cross the parallel walls at y ~ 2^61 -- events
the oracle never has. The native walker now decides the crossing's existence
on the exact line (GAME_SPEC 31.3). These are the Stage 87 `mixed_chain`
scene with the plan the generator skipped ([1 L, 2 R, 3 R], realized the
other way round by Stage 111's rule) and the same chain through a circle
normal and a directional projective. Same format as gen_fixtures_stage88.py.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage112_parallel.py
Writes bench/fixtures/stage112_parallel.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import REFL, line, room  # noqa: E402
from gen_fixtures_stage85 import circle  # noqa: E402
from gen_fixtures_stage87 import proj  # noqa: E402
from gen_fixtures_stage85 import fixture  # noqa: E402
from gen_fixtures_stage88 import pair  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BOX = 2000.0


def scene(projective):
    """mirror 1 (x = 400), the projective 2 (its exit vertical through x = 250),
    mirror 3 (y = 450), the terminal box 10-13 at +-1000"""
    return [line(1, (400, -500), (400, 200), REFL, REFL), projective,
            line(3, (0, 450), (380, 450), REFL, REFL)] + room(10, -1000, -1000, 1000, 1000)


def inside_box(f):
    return all(abs(mp.mpf(e["x"])) <= BOX and abs(mp.mpf(e["y"])) <= BOX for e in f["expected"] if "x" in e)


def main():
    mp.mp.dps = 80
    T.EMIT_INFINITY = True
    fx = []
    player = (200.0, -200.0)
    cursor = (250.0, 300.0)
    plan_of = {"line_normal": [(1, "LEFT"), (2, "RIGHT"), (3, "RIGHT")],
               "circle_normal": [(1, "LEFT"), (2, "LEFT"), (3, "RIGHT")],  # the circle's outside
               "directional": [(1, "LEFT"), (2, "RIGHT"), (3, "RIGHT")]}
    # the K10 case: a line normal projective (the foot of the cursor's image)
    ln = line(2, (0, 200), (380, 200), *(proj("line_normal_projection", False),) * 2)
    # a circle normal projective: the full circle centred (250, 240), radius
    # 40, met first at its bottom (250, 200) -- the planned hit point -- and
    # left along the radial through the centre, vertical, out of its top
    cn = circle(2, (250.0, 240.0), 40.0, *(proj("circle_normal_projection", False),) * 2)
    # a directional projective along (0, 1): the exit is the given direction
    dr = line(2, (0, 200), (380, 200), *(proj("line_directional_projection", False, (0.0, 1.0)),) * 2)
    for tag, p2 in (("line_normal", ln), ("circle_normal", cn), ("directional", dr)):
        plan = plan_of[tag]
        f = pair(fx, "k10_" + tag, scene(p2), player, cursor, plan)
        ks = [(e["kind"], e["surface_id"], e["applied"]) for e in f["expected"]]
        assert ("hit", 2, True) in ks and ("hit", 3, True) in ks, (tag, ks)
        assert inside_box(f), (tag, [(e["kind"], e["surface_id"], e.get("y")) for e in f["expected"]])
        # the vertical exit meets no vertical wall before the wrap: after the
        # reflection at 3 the next hit is the top wall 12
        i3 = ks.index(("hit", 3, True))
        assert ks[i3 + 1][:2] == ("hit", 12), (tag, ks[i3:i3 + 3])
        # stable across the oracle's precisions (a tie-rich scene would not be)
        for dps in (60, 120):
            with mp.workdps(dps):
                g = fixture(f["name"], f["surfaces"], player, plan=plan, mode="planned", cursor=cursor)
            assert [(e["kind"], e["surface_id"], e["applied"]) for e in g["expected"]] == ks, (tag, dps)
    out = os.path.join(ROOT, "bench", "fixtures", "stage112_parallel.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    print("wrote %d fixtures to %s" % (len(fx), out))


if __name__ == "__main__":
    main()
