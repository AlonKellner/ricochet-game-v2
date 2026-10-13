"""Stage 85 fixtures: state flags (ConfigResolver + StateChange), traced by the
oracle (trace2). Same fixture format as gen_fixtures_stage81.py, plus the
initial "flags" and, per event, "state" (the side's state change fired) and
"flags" (the flags after the event).

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage85.py
Writes bench/fixtures/stage85_flags.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import INV, REFL, TERM, ev_json, line, room  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def toggle(key):
    return {"type": "pass", "interactive": True, "state": {"key": key, "op": "toggle", "value": True}}


def setter(key, value, effect=None):
    c = dict(effect) if effect else {"type": "pass", "interactive": True}
    c["state"] = {"key": key, "op": "set", "value": value}
    return c


def categorical(surf, key, table):
    """surf's sides come from the table: {flag_key: (left, right)}."""
    out = dict(surf)
    out["left"] = None
    out["right"] = None
    out["resolver"] = {"key": key, "table": {k: {"left": l, "right": r} for k, (l, r) in table.items()}}
    return out


def circle(sid, center, radius, left=None, right=None):
    cx, cy = center
    return {"id": sid, "line": False, "start": [cx + radius, cy], "end": [cx - radius / 2, cy - radius * 0.75],
            "via": [cx - radius / 2, cy + radius * 0.75], "full": True,
            "circle": {"center": [cx, cy], "radius": radius}, "left": left, "right": right}


def fixture(name, surfs, player, plan=(), mode="physical", direction=None, cursor=None, flags=None):
    sc = T.scene_from_json(surfs)
    plan = [tuple(p) for p in plan]
    if direction is not None:
        m = T.PHYSICAL if mode == "physical" else T.PLANNED
        w = T.trace_mode_direction(sc, player, mp.mpc(*direction), plan,
                                   None if cursor is None else T.P(cursor), m, flags=flags)
    elif mode == "physical":
        w = T.physical(sc, player, cursor, plan, flags)
    else:
        w = T.planned(sc, player, cursor, plan, flags)
    expected = []
    for e in w.events:
        o = ev_json(e)
        o["state"] = bool(e.state)
        o["flags"] = dict(e.flags) if e.flags is not None else None
        expected.append(o)
    return {"name": name, "surfaces": surfs, "player": list(player),
            "direction": None if direction is None else list(direction),
            "cursor": None if cursor is None else list(cursor),
            "plan": [{"surface_id": a, "side": b} for a, b in plan], "mode": mode,
            "flags": dict(flags or {}),
            "expected": expected, "targets": sorted(w.targets)}


def kinds(f):
    return [(e["kind"], e["surface_id"], e["side"], e["applied"], e["state"]) for e in f["expected"]]


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(10, -1000, -1000, 1000, 1000)
    # a toggler (pass + toggle "open") at x = 300, a door mirror at x = 600
    # that reflects only while "open" is true
    tog = line(1, (300, -400), (300, 400), toggle("open"), toggle("open"))
    door = categorical(line(2, (600, -400), (600, 400)), "open",
                       {"true": (REFL, REFL), "DEFAULT": (None, None)})
    f = fixture("categorical_mirror_switches_mid_trace", [tog, door] + walls, (100.0, 0.0), direction=(1.0, 0.0625))
    assert [k[1] for k in kinds(f) if k[3] or k[4]][:3] == [1, 2, 1], kinds(f)
    fx.append(f)
    # the same scene, the beam crossing the toggler's carrier off-segment
    # (above it): the state change does not fire, the door stays a pass
    f = fixture("state_change_off_segment_does_not_fire", [tog, door] + walls, (100.0, -600.0), direction=(1.0, 0.0625))
    assert not any(e["state"] for e in f["expected"]), kinds(f)
    fx.append(f)
    # the initial flags open the door
    f = fixture("initial_flags_resolve", [door] + walls, (100.0, 0.0), direction=(1.0, 0.0625), flags={"open": True})
    assert f["expected"][0]["applied"], kinds(f)
    fx.append(f)
    # last writer wins: SET k = 1 at x = 200, SET k = 2 at x = 400; the gate at
    # x = 600 is a terminal for 1 and a mirror for 2
    s1 = line(1, (200, -400), (200, 400), setter("k", 1), setter("k", 1))
    s2 = line(2, (400, -400), (400, 400), setter("k", 2), setter("k", 2))
    gate = categorical(line(3, (600, -400), (600, 400)), "k", {"1": (TERM, TERM), "2": (REFL, REFL)})
    f = fixture("last_writer_wins_along_trace", [s1, s2, gate] + walls, (100.0, 0.0), direction=(1.0, 0.0625))
    assert f["expected"][2]["applied"] and not f["expected"][2]["terminal"], kinds(f)
    fx.append(f)
    # an effect and a state change on the same side: an inversion that sets k
    inv_set = circle(1, (500.0, 0.0), 200.0, setter("k", True, INV), setter("k", True, INV))
    f = fixture("effect_and_state_change_coexist", [inv_set] + walls, (100.0, -60.0), direction=(1.0, 0.0))
    assert f["expected"][0]["applied"] and f["expected"][0]["state"], kinds(f)
    fx.append(f)
    # pops restore beams, not flags: inversion circle, a toggler inside it on
    # the inverted beam, the re-hit of the circle pops
    inv = circle(1, (500.0, 0.0), 200.0, INV, INV)
    tin = line(2, (480.0, -700.0), (480.0, 40.0), toggle("k"), toggle("k"))
    f = fixture("pops_restore_beams_not_flags", [inv, tin] + walls, (100.0, -60.0), direction=(1.0, 0.0))
    ks = kinds(f)
    assert ks[0][1] == 1 and ks[0][3] and ks[1][1] == 2 and ks[1][4] and ks[2][1] == 1 and ks[2][3], ks
    fx.append(f)
    # PLANNED: a consumed plan entry fires its state change (even off-segment)
    fx.append(fixture("planned_applies_state_on_consumed_entry", [tog, door] + walls, (100.0, -600.0),
                      plan=[(1, "RIGHT"), (2, "RIGHT")], mode="planned", cursor=(450.0, -560.0)))
    # forward state simulation: without the toggler entry the door resolves to
    # a pass (consumed, no frame change: aimed straight at the cursor) ...
    fx.append(fixture("planner_forward_state_simulation_pass", [tog, door] + walls, (100.0, 0.0),
                      plan=[(2, "RIGHT")], mode="planned", cursor=(800.0, 100.0)))
    # ... with it the door is a mirror (aimed at the cursor's image); the
    # cursor lies before the reflected beam re-crosses the toggler
    for mode in ("planned", "physical"):
        fx.append(fixture("planner_forward_state_simulation_%s" % mode, [tog, door] + walls, (100.0, 0.0),
                          plan=[(1, "RIGHT"), (2, "RIGHT")], mode=mode, cursor=(450.0, 40.0)))
    out = os.path.join(ROOT, "bench", "fixtures", "stage85_flags.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], kinds(f)[:6], len(f["expected"]), f["expected"][-1]["flags"])


if __name__ == "__main__":
    main()
