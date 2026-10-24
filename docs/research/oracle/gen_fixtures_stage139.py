"""Stage 139 fixtures: off-segment crossings are not events (GAME_SPEC 12.1, 12.6, 4.3).

Usage:
  docs/research/.venv/bin/python -I docs/research/oracle/gen_fixtures_stage139.py

Writes bench/fixtures/stage139_off_segment.json: walks past surfaces whose
carriers the beam crosses off their segments. Such a crossing on a surface no
remaining plan entry names is no event -- not counted by the 64 limit, not
drawn -- while a plan surface's crossing stays an event on or off its segment
(the quiet entry), a candidate crossed on its segment on a side that passes
stays an event, and a circle beam (after an inversion) drops its off-segment
crossings too.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import PASS, REFL, TERM, line, room  # noqa: E402
from gen_fixtures_stage85 import circle, fixture, kinds  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
INV = {"type": "inversion", "interactive": True}


def walls():
    return room(first_id=90, x0=-100, y0=-500, x1=900, y1=500)


def no_off_segment(f):
    """No event of the walk is an off-segment hit of a surface the plan does not name."""
    named = {p["surface_id"] for p in f["plan"]}
    return all(e["kind"] != "hit" or e["on_segment"] or e["surface_id"] in named for e in f["expected"])


def thin(sid, x, y1=-500, y2=500):
    """A candidate a ray along +x crosses on its segment that fires nothing: a wall
    on its far side only."""
    s = line(sid, (x, y1), (x, y2), PASS, TERM)
    probe = T.trace_physical_direction(T.scene_from_json([s]), (x - 10.0, (y1 + y2) / 2.0), mp.mpc(1, 0))
    return s if not probe.events[0].applied else line(sid, (x, y2), (x, y1), PASS, TERM)


def main():
    mp.mp.dps = 80
    fx = []
    # (i) a mirror whose segment does not reach the ray: no event; planned, the
    # mirror is the plan's surface and its off-segment crossing stays an event
    m = line(1, (100, 10), (100, 500), REFL, REFL)
    f = fixture("offseg_mirror_physical", [m] + walls(), (0.0, 0.0), direction=(1.0, 0.0))
    assert kinds(f) == [("hit", 91, "LEFT", True, False)] or (len(f["expected"]) == 1 and f["expected"][0]["terminal"]), kinds(f)
    assert no_off_segment(f)
    fx.append(f)
    for side in ("LEFT", "RIGHT"):
        f = fixture("offseg_mirror_planned_" + side.lower(), [m] + walls(), (0.0, 0.0), plan=[(1, side)], mode="planned",
                    direction=(1.0, 0.0))
        e0 = f["expected"][0]
        # (the planned walk consumes its entry at the first crossing of the surface and
        # side, on or off the segment: applied for the matching side)
        assert e0["kind"] == "hit" and e0["surface_id"] == 1 and not e0["on_segment"], kinds(f)
        fx.append(f)
    # (ii) seventy walls crossed off their segments, then the room's wall: one event
    # (before Stage 139 the 64th crossing cut the trace)
    many = [line(i + 1, (10.0 * (i + 1), 100), (10.0 * (i + 1), 200), TERM, TERM) for i in range(70)]
    f = fixture("offseg_seventy_walls", many + walls(), (0.0, 0.0), direction=(1.0, 0.0))
    assert len(f["expected"]) == 1 and f["expected"][0]["terminal"] and f["expected"][0]["surface_id"] >= 90, kinds(f)
    fx.append(f)
    # (iii) a planned wall crossed off its segment: the entry is consumed quietly
    # (B1), the event stays, the walk goes on
    w1 = line(1, (100, 10), (100, 500), TERM, TERM)
    probe = fixture("probe", [w1] + walls(), (0.0, 0.0), plan=[(1, "LEFT")], direction=(1.0, 0.0))
    side = probe["expected"][0]["side"]
    f = fixture("planned_wall_offseg_quiet", [w1] + walls(), (0.0, 0.0), plan=[(1, side)], direction=(1.0, 0.0))
    ks = kinds(f)
    assert ks[0][:2] == ("hit", 1) and not f["expected"][0]["on_segment"] and not f["expected"][0]["applied"], ks
    assert len(ks) == 2 and ks[1][3], ks   # then the room's wall
    fx.append(f)
    # (iv) a candidate crossed on its segment on a side that passes: an event, counted
    t4 = thin(4, 150)
    f = fixture("on_segment_pass_side_event", [m, t4] + walls(), (0.0, 0.0), direction=(1.0, 0.0))
    ks = kinds(f)
    assert ks[0][:2] == ("hit", 4) and f["expected"][0]["on_segment"] and not f["expected"][0]["applied"] and len(ks) == 2, ks
    fx.append(f)
    # (v) a hair off an endpoint: the mirror starts 2^-40 above the ray: off its
    # segment, no event (decided exactly by the oracle; the ladder by its margin)
    hair = line(1, (100, 2.0 ** -40), (100, 500), REFL, REFL)
    f = fixture("hair_off_endpoint", [hair] + walls(), (0.0, 0.0), direction=(1.0, 0.0))
    assert len(f["expected"]) == 1 and f["expected"][0]["terminal"], kinds(f)
    fx.append(f)
    # (vi) a circle beam: an inversion circle, then a mirror whose carrier the
    # image circle crosses off the segment (placed from the oracle's own beam)
    inv = circle(1, (300.0, 0.0), 100.0, INV, INV)
    w = T.trace_physical_direction(T.scene_from_json([inv] + walls()), (0.0, 40.0), mp.mpc(1, 0))
    assert w.events[0].kind == "hit" and w.events[0].applied, [e.kind for e in w.events]
    b = w.events[1].beam
    assert b.kappa != 0, "the image beam is a circle"
    t_mid = (w.events[1].t if w.events[1].t is not None else mp.mpf(1)) / 2
    pt = b.at(t_mid)
    tang = b.tangent_at(t_mid)
    normal = mp.mpc(-tang.imag, tang.real) / abs(tang)
    # the mirror's carrier is the normal line through pt; its segment starts 30 px
    # along the normal from pt and runs 60 px further: the crossing at pt is off it
    s0 = pt + 30 * normal
    s1 = pt + 90 * normal
    far = line(2, (float(s0.real), float(s0.imag)), (float(s1.real), float(s1.imag)), REFL, REFL)
    f = fixture("circle_beam_offseg_mirror", [inv, far] + walls(), (0.0, 40.0), direction=(1.0, 0.0))
    assert not any(e["kind"] == "hit" and e["surface_id"] == 2 for e in f["expected"]), kinds(f)
    assert no_off_segment(f)
    fx.append(f)
    out = {"dps": mp.mp.dps, "fixtures": fx}
    path = os.path.join(ROOT, "bench", "fixtures", "stage139_off_segment.json")
    with open(path, "w") as fh:
        json.dump(out, fh, indent=1)
        fh.write("\n")
    print("wrote", path, len(fx), "fixtures")


if __name__ == "__main__":
    main()
