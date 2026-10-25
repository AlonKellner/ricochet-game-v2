"""Stage 156 fixtures: arrow states -- conditional state changes and frost (GAME_SPEC 10.6).

Usage:
  docs/research/.venv/bin/python -I docs/research/oracle/gen_fixtures_stage156.py

Writes bench/fixtures/stage156_arrow_states.json: physical (and some planned)
walks in a room of walls.
- Frost: a frost barrier (a pass writing shot:frost) frosts the arrow; the next
  wall reflects it and uses the frost up; the wall after stops it. A frosted
  arrow inverts off a terminal circle; a mirror and a pass keep the frost; a
  rigid wall stops a frosted arrow.
- Conditional changes (each read under the flags the side fired under): fire
  passes from a burning surface to the arrow and from a burning arrow to a
  flammable surface; charge swaps between the arrow and a conductive surface.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_fixtures_stage81 import PASS, REFL, TERM, line, room  # noqa: E402
from gen_fixtures_stage85 import circle, fixture, kinds  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
FROST_BARRIER = {"type": "pass", "interactive": False, "states": [{"key": "shot:frost", "op": "set", "value": True}]}
RIGID = {"type": "terminal", "interactive": False, "rigid": True}


def on(key):
    return [{"key": key, "values": ["true"]}]


def off(key):
    return [{"key": key, "values": ["DEFAULT", "false"]}]


def flammable(name, face):
    return dict(face, states=[{"key": "el:%s:fire" % name, "op": "set", "value": True, "when": on("shot:fire")},
                              {"key": "shot:fire", "op": "set", "value": True, "when": on("el:%s:fire" % name)}])


def conductive(name, face):
    give = on("shot:charge") + off("el:%s:charge" % name)
    take = off("shot:charge") + on("el:%s:charge" % name)
    return dict(face, states=[{"key": "shot:charge", "op": "set", "value": False, "when": give},
                              {"key": "el:%s:charge" % name, "op": "set", "value": True, "when": give},
                              {"key": "shot:charge", "op": "set", "value": True, "when": take},
                              {"key": "el:%s:charge" % name, "op": "set", "value": False, "when": take}])


def walls():
    return room(first_id=90, x0=-400, y0=-500, x1=900, y1=500)


def last_flags(f):
    return f["expected"][-1]["flags"]


def main():
    mp.mp.dps = 80
    fx = []
    # frost: the barrier at x = 100, a wall at x = 300 tilted (the reflection goes up), the room's top wall stops it
    barrier = line(1, (100, -200), (100, 200), FROST_BARRIER, FROST_BARRIER)
    wall = line(2, (300, -100), (250, 100), TERM, TERM)
    f = fixture("frost_wall_reflects_once", [barrier, wall] + walls(), (0.0, 0.0), direction=(1.0, 0.0))
    fx.append(f)
    hits = [e for e in f["expected"] if e["kind"] == "hit"]
    assert hits[1]["surface_id"] == 2 and not hits[1]["terminal"] and hits[1]["flags"].get("shot:frost") is False, kinds(f)
    assert hits[-1]["terminal"] and hits[-1]["surface_id"] >= 90, kinds(f)
    # the same, planned with the barrier and the wall's side: both walks agree (the frosted side is a reflection)
    fx.append(fixture("frost_wall_planned", [barrier, wall] + walls(), (0.0, 0.0), direction=(1.0, 0.0), mode="planned",
                      plan=[(1, "RIGHT")]))
    # a frosted arrow (frost from the start) inverts off a terminal circle
    f = fixture("frost_circle_inverts", [circle(3, (300, 0), 80, TERM, TERM)] + walls(), (0.0, 0.0), direction=(1.0, 0.0),
                flags={"shot:frost": True})
    fx.append(f)
    first = [e for e in f["expected"] if e["kind"] == "hit"][0]
    assert first["surface_id"] == 3 and not first["terminal"] and first["flags"].get("shot:frost") is False, kinds(f)
    # a mirror and a pass keep the frost; the wall after uses it
    m = line(4, (200, -100), (200, 100), REFL, REFL)
    f = fixture("frost_kept_by_mirror", [m] + walls(), (0.0, 0.0), direction=(1.0, 0.0), flags={"shot:frost": True})
    fx.append(f)
    hits = [e for e in f["expected"] if e["kind"] == "hit"]
    assert hits[0]["surface_id"] == 4 and hits[0]["flags"].get("shot:frost") is True, kinds(f)
    # a rigid wall stops a frosted arrow (the frost kept)
    f = fixture("frost_rigid_stops", [line(5, (200, -100), (200, 100), RIGID, RIGID)] + walls(), (0.0, 0.0), direction=(1.0, 0.0),
                flags={"shot:frost": True})
    fx.append(f)
    first = [e for e in f["expected"] if e["kind"] == "hit"][0]
    assert first["surface_id"] == 5 and first["terminal"] and first["flags"].get("shot:frost") is True, kinds(f)
    # fire: a burning pass surface sets the arrow on fire; a flammable wall after it catches fire
    torch = line(6, (100, -200), (100, 200), flammable("torch", PASS), flammable("torch", PASS))
    wood = line(7, (300, -200), (300, 200), flammable("wood", TERM), flammable("wood", TERM))
    f = fixture("fire_torch_to_wood", [torch, wood] + walls(), (0.0, 0.0), direction=(1.0, 0.0), flags={"el:torch:fire": True})
    fx.append(f)
    assert last_flags(f).get("shot:fire") is True and last_flags(f).get("el:wood:fire") is True, kinds(f)
    # unlit: nothing burns
    f = fixture("fire_unlit", [torch, wood] + walls(), (0.0, 0.0), direction=(1.0, 0.0))
    fx.append(f)
    assert "el:wood:fire" not in last_flags(f) and "shot:fire" not in last_flags(f), kinds(f)
    # charge: a charged arrow gives its charge to a conductor (pass), then takes another's (charged) at a wall
    c1 = line(8, (100, -200), (100, 200), conductive("c1", PASS), conductive("c1", PASS))
    c2 = line(9, (300, -200), (300, 200), conductive("c2", TERM), conductive("c2", TERM))
    f = fixture("charge_swaps", [c1, c2] + walls(), (0.0, 0.0), direction=(1.0, 0.0), flags={"shot:charge": True, "el:c2:charge": True})
    fx.append(f)
    fl = last_flags(f)
    assert fl.get("el:c1:charge") is True and fl.get("el:c2:charge") is False and fl.get("shot:charge") is True, (kinds(f), fl)
    # both charged: nothing happens
    f = fixture("charge_both", [c1] + walls(), (0.0, 0.0), direction=(1.0, 0.0), flags={"shot:charge": True, "el:c1:charge": True})
    fx.append(f)
    assert last_flags(f) == {"shot:charge": True, "el:c1:charge": True}, last_flags(f)
    out = os.path.join(ROOT, "bench", "fixtures", "stage156_arrow_states.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(k[0][:3], k[1], k[3]) for k in kinds(f)][:6])


if __name__ == "__main__":
    main()
