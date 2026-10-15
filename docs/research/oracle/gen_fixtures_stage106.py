"""Stage 106 fixtures: the departure point after a pop (GAME_SPEC 11.4; K2).

A pop (the stack top's surface hit again, the old beam restored) tags the pop
point as the restored beam's departure point, like a push does, so a known
point there (the player or the cursor exactly on a circle mirror) never snaps
the departing carrier's other crossing onto it; on a resumed beam the
departing carrier's root nearest the departure parameter is the departure.
Every case asserts that a pop tagged a known point (a spy on _tag_departure),
so a fixture cannot pass by a lucky rounding.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage106.py
Writes bench/fixtures/stage106_pop_departure.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import PASS, REFL, line  # noqa: E402
from gen_fixtures_stage85 import circle, fixture, kinds  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
INV = {"type": "inversion", "interactive": True}
TERM = {"type": "terminal", "interactive": False}
BOX = [line(90, (0.0, 0.0), (1920.0, 0.0), TERM, TERM), line(91, (1920.0, 0.0), (1920.0, 1080.0), TERM, TERM),
       line(92, (1920.0, 1080.0), (0.0, 1080.0), TERM, TERM), line(93, (0.0, 1080.0), (0.0, 0.0), TERM, TERM)]


class Spy:
    """Records the (point, surface id) pairs a pop tagged as departure points."""

    def __init__(self):
        self.tagged = []

    def __enter__(self):
        self.orig = T._tag_departure

        def spy(known, q, t_res, sid, pt_exact, portal):
            out = self.orig(known, q, t_res, sid, pt_exact, portal)
            for k in out:
                if k[2] == sid and k[1] == t_res and not any(o is k or (o[0] == k[0] and o[2] == sid) for o in known):
                    self.tagged.append((k[0], sid))
            return out
        T._tag_departure = spy
        return self

    def __exit__(self, *a):
        T._tag_departure = self.orig

    def fired(self, sid, pt=None):
        return any(s == sid and (pt is None or p == T.P(pt)) for p, s in self.tagged)


def hits(f):
    return [(e["surface_id"], e["applied"]) for e in f["expected"] if e["kind"] == "hit"]


def no_reapply_in_place(f):
    last = None
    for e in f["expected"]:
        pt = (e.get("x"), e.get("y"))
        if e["kind"] in ("cursor", "infinity") and last is not None and pt == (last.get("x"), last.get("y")):
            continue
        if e["kind"] == "hit" and e["applied"] and last is not None and e["surface_id"] == last["surface_id"] \
                and pt == (last.get("x"), last.get("y")):
            return False
        last = e if e["kind"] == "hit" and e["applied"] else None
    return True


def main():
    mp.mp.dps = 80
    assert T.EMIT_INFINITY
    fx = []
    # (i) K2, from tangent_infinity.tscn: the player on the inversion circle's top,
    # the aim image under the mirror plan at its bottom
    dump = json.load(open(os.path.join(ROOT, "bench", "scenes", "playground__tangent_infinity.surfaces.json")))
    for mode in ("planned", "physical"):
        with Spy() as s:
            f = fixture("k2_player_on_inversion_circle_" + mode, dump["surfaces"], (1160.0, 440.0), plan=[(6, "LEFT")],
                        mode=mode, cursor=(760.0, 640.0))
        # (planned: the unplanned inversion is a pass until the cursor; the pop is physical)
        assert mode == "planned" or s.fired(8, (1160.0, 440.0)), kinds(f)
        assert no_reapply_in_place(f), kinds(f)
        fx.append(f)
    with Spy() as s:
        ref = fixture("k2_reference_aimed_at_the_image", dump["surfaces"], (1160.0, 440.0), mode="physical", cursor=(1160.0, 640.0))
    assert s.fired(8, (1160.0, 440.0)) and hits(ref) == hits(fx[-1]), (kinds(ref), kinds(fx[-1]))
    fx.append(ref)
    # (ii) the cursor on a reflective arc (the right half of the circle (960, 540) r 200):
    # a pop at the cursor by the departure re-hit, then the line goes on
    arc = {"id": 1, "line": False, "start": [960.0, 340.0], "end": [960.0, 740.0], "via": [1160.0, 540.0],
           "full": False, "left": REFL, "right": REFL}
    for mode in ("planned", "physical"):
        with Spy() as s:
            f = fixture("cursor_on_arc_pop_" + mode, [arc] + BOX, (600.0, 300.0), mode=mode, cursor=(1160.0, 540.0))
        assert s.fired(1, (1160.0, 540.0)) and no_reapply_in_place(f), kinds(f)
        fx.append(f)
    # (iii) a closed orbit: the player on an inversion circle, aiming through the center
    c = circle(1, (960.0, 540.0), 200.0, INV, INV)
    with Spy() as s:
        f = fixture("periodic_orbit_direction", [c] + BOX, (760.0, 540.0), direction=(1.0, 0.0))
    assert s.fired(1, (760.0, 540.0)) and f["expected"][-1]["kind"] == "max", kinds(f)
    assert all(h == (1, True) for h in hits(f)) and len(hits(f)) >= 20, kinds(f)
    fx.append(f)
    with Spy() as s:
        f = fixture("periodic_orbit_aimed_physical", [c] + BOX, (760.0, 540.0), mode="physical", cursor=(1160.0, 540.0))
    assert s.fired(1, (760.0, 540.0)), kinds(f)
    fx.append(f)
    # (iv) a known point at the other crossing: the cursor on the circle, the player inside
    with Spy() as s:
        f = fixture("cursor_at_the_other_crossing_planned", [c] + BOX, (860.0, 540.0), mode="planned", cursor=(1160.0, 540.0))
    assert f["expected"][0]["kind"] == "cursor" and no_reapply_in_place(f), kinds(f)
    fx.append(f)
    # (v) a portal pop by the departure re-hit: unchanged behaviour (no tag of a rounded point)
    portal_a = {"type": "portal", "interactive": True, "d": [800.0, 0.0], "theta": 0.0, "pair": 2}
    portal_b = {"type": "portal", "interactive": True, "d": [-800.0, 0.0], "theta": 0.0, "pair": 1}
    surfs = [line(1, (400.0, 200.0), (400.0, 700.0), portal_a, portal_a), line(2, (1200.0, 200.0), (1200.0, 700.0), portal_b, portal_b),
             line(3, (1500.0, 100.0), (1500.0, 900.0), REFL, REFL)] + BOX
    with Spy() as s:
        f = fixture("portal_pop_by_rehit_direction", surfs, (300.0, 450.0), direction=(1.0, 0.0))
    assert hits(f)[:3] == [(1, True), (3, True), (2, True)] and no_reapply_in_place(f), kinds(f)
    fx.append(f)
    # (vi) K3: a degenerate aim: the cursor is the player's mirror image in the planned
    # mirror (every direction solves the plan): straight at the cursor, the circle
    # through the player, the cursor and Q0 (not the circle tangent to that direction)
    for mode in ("planned", "physical"):
        f = fixture("k3_degenerate_aim_through_the_cursor_" + mode, dump["surfaces"], (835.0, 665.0), plan=[(6, "LEFT")],
                    mode=mode, cursor=(1085.0, 665.0))
        assert f["expected"][0]["kind"] == "hit" and f["expected"][0]["surface_id"] == 6 and not f["expected"][0]["applied"], kinds(f)
        assert no_reapply_in_place(f), kinds(f)
        fx.append(f)
    out = os.path.join(ROOT, "bench", "fixtures", "stage106_pop_departure.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(k[0][:3], k[1], k[3]) for k in kinds(f)][:8])


if __name__ == "__main__":
    main()
