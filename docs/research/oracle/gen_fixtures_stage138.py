"""Stage 138 fixtures: shared points stop the arrow (GAME_SPEC 11.1, 11.4, 12.1, 14.1).

Usage:
  docs/research/.venv/bin/python -I docs/research/oracle/gen_fixtures_stage138.py

Writes bench/fixtures/stage138_shared_points.json: physical and planned walks
through a corner, a T-junction, proper crossings (a mirror and a wall; a mirror
and a toggler; two togglers; a mirror and a pure pass, which the mirror owns)
and a lone endpoint (a graze), each aimed exactly at the point, in a room of
walls. The physical walk stops at a shared point two or more surfaces have a
consequence at, nothing firing; the planned walk, computing its entries alone,
follows its plan there (the walks diverge).
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import PASS, REFL, TERM, line, room  # noqa: E402
from gen_fixtures_stage85 import fixture, kinds  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TOGGLE_A = {"type": "pass", "interactive": False, "states": [{"key": "a", "op": "toggle", "value": True}]}
TOGGLE_B = {"type": "pass", "interactive": False, "states": [{"key": "b", "op": "toggle", "value": True}]}


def both(fx, name, surfs, player, direction, plan):
    """The physical walk from the direction, and the planned walk of the plan (no
    cursor: the walk runs to the room's walls)."""
    surfs = list(surfs) + room(first_id=90, x0=-400, y0=-500, x1=400, y1=500)
    q = fixture(name + "_physical", surfs, player, direction=direction)
    p = fixture(name + "_planned", surfs, player, plan=plan, mode="planned", direction=direction)
    fx += [q, p]
    return q, p


def terminal_first(f):
    e = f["expected"]
    return len(e) == 1 and e[0]["kind"] == "hit" and e[0]["terminal"]


def first_hit(f):
    e = f["expected"][0]
    return e["surface_id"], e["applied"], e["terminal"], e["state"]


def main():
    mp.mp.dps = 80
    fx = []
    # a corner: two mirrors meeting at (100, 0), the beam at the vertex
    a = line(1, (100, 0), (150, -100), REFL, REFL)
    b = line(2, (100, 0), (150, 100), REFL, REFL)
    q, p = both(fx, "corner", [a, b], (0.0, 0.0), (1.0, 0.0), [(2, "RIGHT")])
    assert terminal_first(q) and first_hit(q)[0] == 1 and first_hit(p) == (2, True, False, False), (kinds(q), kinds(p))
    # a T-junction: a mirror's endpoint on a wall's interior
    wall = line(1, (100, -200), (100, 200), TERM, TERM)
    m = line(2, (100, 0), (300, 50), REFL, REFL)
    q, p = both(fx, "t_junction", [wall, m], (0.0, 0.0), (1.0, 0.0), [(2, "RIGHT")])
    assert terminal_first(q) and first_hit(q)[0] == 1 and first_hit(p) == (2, True, False, False), (kinds(q), kinds(p))
    # a proper crossing of a mirror and a wall
    m = line(1, (100, -100), (100, 100), REFL, REFL)
    wall = line(2, (0, -50), (200, 50), TERM, TERM)
    q, p = both(fx, "crossing_mirror_wall", [m, wall], (0.0, 0.0), (1.0, 0.0), [(1, "RIGHT")])
    assert terminal_first(q) and first_hit(q)[0] == 1 and first_hit(p) == (1, True, False, False), (kinds(q), kinds(p))
    # a mirror and a toggler: two consequences at one point -- the arrow stops, nothing fires
    q, p = both(fx, "crossing_mirror_toggler", [m, line(2, (0, -50), (200, 50), TOGGLE_B, TOGGLE_B)], (0.0, 0.0), (1.0, 0.0), [(1, "RIGHT")])
    assert terminal_first(q) and first_hit(q) == (1, True, True, False) and q["expected"][0]["flags"] == {}, (kinds(q), kinds(p))
    # two togglers: the same
    ta = line(1, (100, -100), (100, 100), TOGGLE_A, TOGGLE_A)
    tb = line(2, (0, -50), (200, 50), TOGGLE_B, TOGGLE_B)
    q, p = both(fx, "crossing_togglers", [ta, tb], (0.0, 0.0), (1.0, 0.0), [(1, "LEFT"), (2, "LEFT")])
    assert terminal_first(q) and q["expected"][0]["flags"] == {}, (kinds(q), kinds(p))
    # a mirror and a pure pass: the mirror alone has a consequence, so it owns the hit and reflects
    q, p = both(fx, "crossing_mirror_pass", [m, line(2, (0, -50), (200, 50), PASS, PASS)], (0.0, 0.0), (1.0, 0.0), [(1, "RIGHT")])
    assert first_hit(q) == (1, True, False, False), kinds(q)
    # a lone endpoint: a graze, the beam goes on to the room's wall
    q, p = both(fx, "lone_endpoint", [line(1, (100, 0), (100, 100), REFL, REFL)], (0.0, 0.0), (1.0, 0.0), [(1, "LEFT")])
    assert [(e["surface_id"], e["applied"]) for e in q["expected"] if e["kind"] == "hit"][:2] == [(1, False), (91, True)], kinds(q)
    out = os.path.join(ROOT, "bench", "fixtures", "stage138_shared_points.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(k[0][:3], k[1], k[3]) for k in kinds(f)][:6])


if __name__ == "__main__":
    main()
