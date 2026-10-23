"""Stage 132 fixtures: the ray's origin as a plan entry (GAME_SPEC 4.2, 12.6, 13.1).

Usage:
  docs/research/.venv/bin/python -I docs/research/oracle/gen_fixtures_stage132.py

Writes bench/fixtures/stage132_origin_entry.json: planned and physical walks of
plans with origin entries -- consumed by the passage in both walks; the
realization by the parity of infinity and origin entries.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import PASS, REFL, line  # noqa: E402
from gen_fixtures_stage85 import circle, fixture, kinds  # noqa: E402
from gen_fixtures_stage87 import proj  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
INF = (T.INFINITY_ID, "")
ORG = (T.ORIGIN_ID, "")


def reaches(f):
    return any(e["kind"] == "cursor" for e in f["expected"])


def origins(f):
    return [e["applied"] for e in f["expected"] if e["kind"] == "origin"]


def pair(fx, name, surfs, player, cursor, plan):
    p = fixture(name + "_planned", surfs, player, plan=plan, mode="planned", cursor=cursor)
    q = fixture(name + "_physical", surfs, player, plan=plan, mode="physical", cursor=cursor)
    fx += [p, q]
    return p, q


def main():
    mp.mp.dps = 80
    fx = []
    # an open scene: [inf, O] aims at the cursor, passes it pending, wraps, passes
    # the player and reaches it -- aligned
    p_line = line(1, (500, -500), (500, 500), PASS, PASS)
    p, q = pair(fx, "open_infinity_then_origin", [p_line], (0.0, 0.0), (300.0, 0.0), [INF, ORG])
    assert reaches(p) and reaches(q) and origins(q)[0] is True, kinds(q)
    # [O] alone: the wrap comes first, unplanned -- the physical walk diverges there
    p, q = pair(fx, "open_origin_alone", [p_line], (0.0, 0.0), (300.0, 0.0), [ORG])
    assert not reaches(q), kinds(q)
    # [inf, inf]: the parity -- an even count aims at the cursor
    p, q = pair(fx, "open_infinity_twice", [p_line], (0.0, 0.0), (300.0, 0.0), [INF, INF])
    # a mirror: past the player's image (400, 0) the origin is planned after the wrap
    m = line(1, (200, -300), (200, 300), REFL, REFL)
    p, q = pair(fx, "mirror_infinity_origin", [m], (0.0, 0.0), (300.0, 25.0), [(1, "RIGHT"), INF, ORG])
    assert reaches(p) and reaches(q) and origins(q)[0] is True, (kinds(p), kinds(q))
    p, q = pair(fx, "mirror_infinity_without_origin", [m], (0.0, 0.0), (300.0, 25.0), [(1, "RIGHT"), INF])
    assert not reaches(q), kinds(q)
    # a circle normal projection toward the centre: its origin, the centre, planned
    c = circle(1, (500.0, 300.0), 100.0, *(proj("circle_normal_projection", False),) * 2)
    p, q = pair(fx, "circle_normal_inward_origin", [c], (200.0, 320.0), (550.0, 300.0), [(1, "LEFT"), ORG])
    assert reaches(p) and reaches(q), (kinds(p), kinds(q))
    p, q = pair(fx, "circle_normal_inward_without_origin", [c], (200.0, 320.0), (550.0, 300.0), [(1, "LEFT")])
    assert not reaches(q), kinds(q)
    out = os.path.join(ROOT, "bench", "fixtures", "stage132_origin_entry.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(e["surface_id"], e["side"]) for e in f["plan"]], [(k[0][:3], k[1], k[3]) for k in kinds(f)][:8], reaches(f))


if __name__ == "__main__":
    main()
