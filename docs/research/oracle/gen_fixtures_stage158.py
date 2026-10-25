"""Stage 158 fixtures: planning with frost, and shared points under frost (GAME_SPEC 10.6, 11.1).

Usage:
  docs/research/.venv/bin/python -I docs/research/oracle/gen_fixtures_stage158.py

Writes bench/fixtures/stage158_frost_planning.json:
- A frost barrier and a wall, both planned: the planned walk reflects off the wall as
  the physical walk does (frost planned and active), the frost used up.
- A frosted arrow aimed exactly at a corner of two walls, and at a T-junction of a
  mirror's end on a wall: the shared point stops it (Stage 138), frost or not.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_fixtures_stage81 import REFL, TERM, line, room  # noqa: E402
from gen_fixtures_stage85 import fixture, kinds  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
FROST_BARRIER = {"type": "pass", "interactive": True, "states": [{"key": "shot:frost", "op": "set", "value": True}]}
SIDE = {0: "LEFT", 1: "RIGHT"}


def walls():
    return room(first_id=90, x0=-400, y0=-500, x1=900, y1=500)


def hits(f):
    return [e for e in f["expected"] if e["kind"] == "hit"]


def main():
    mp.mp.dps = 80
    fx = []
    barrier = line(1, (100, -200), (100, 200), FROST_BARRIER, FROST_BARRIER)
    wall = line(2, (300, -100), (250, 100), TERM, TERM)
    surfs = [barrier, wall] + walls()
    q = fixture("frost_planned_wall_physical", surfs, (0.0, 0.0), direction=(1.0, 0.0))
    hb, hw = hits(q)[0], hits(q)[1]
    plan = [(1, hb["side"] if isinstance(hb["side"], str) else SIDE[hb["side"]]), (2, hw["side"] if isinstance(hw["side"], str) else SIDE[hw["side"]])]
    p = fixture("frost_planned_wall_planned", surfs, (0.0, 0.0), direction=(1.0, 0.0), mode="planned", plan=plan)
    fx += [q, p]
    pw = [e for e in hits(p) if e["surface_id"] == 2]
    assert pw and pw[0]["applied"] and pw[0]["flags"].get("shot:frost") is False, kinds(p)
    # a corner of two walls, aimed exactly at the vertex: a frosted arrow stops there, the frost kept
    a = line(3, (300, 0), (350, -100), TERM, TERM)
    b = line(4, (300, 0), (350, 100), TERM, TERM)
    f = fixture("frost_corner_stops", [a, b] + walls(), (0.0, 0.0), direction=(1.0, 0.0), flags={"shot:frost": True})
    fx.append(f)
    h = hits(f)
    assert len(h) == 1 and h[0]["terminal"] and h[0]["flags"].get("shot:frost") is True, kinds(f)
    # a T-junction: a mirror's end on a wall's interior
    w = line(5, (300, -200), (300, 200), TERM, TERM)
    m = line(6, (300, 0), (500, 50), REFL, REFL)
    f = fixture("frost_t_junction_stops", [w, m] + walls(), (0.0, 0.0), direction=(1.0, 0.0), flags={"shot:frost": True})
    fx.append(f)
    h = hits(f)
    assert len(h) == 1 and h[0]["terminal"] and h[0]["flags"].get("shot:frost") is True, kinds(f)
    out = os.path.join(ROOT, "bench", "fixtures", "stage158_frost_planning.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(k[0][:3], k[1], k[2], k[3]) for k in kinds(f)][:6])


if __name__ == "__main__":
    main()
