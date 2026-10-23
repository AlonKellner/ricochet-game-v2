"""Stage 111 fixtures: the first epoch's realization is fixed by the plan's
first entry (GAME_SPEC 13.1, R1-R3; trace2.realization), traced by the oracle
with EMIT_INFINITY on. Same format as gen_fixtures_stage88.py.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage111_orientation.py
Writes bench/fixtures/stage111_orientation.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import PASS, REFL, TERM, line  # noqa: E402
from gen_fixtures_stage85 import kinds  # noqa: E402
from gen_fixtures_stage88 import INF, pair, reaches  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
INV = {"type": "inversion", "interactive": True}


def mirror_and_wall():
    """scenes/test_levels/mirror_and_wall.tscn: solid lines 1-4, the mirror 5
    (reflect on its L side, the player's; pass on its R side), pass edges 6-9."""
    return [
        line(1, (560, 240), (1360, 240), TERM, TERM), line(2, (1360, 840), (560, 840), TERM, TERM),
        line(3, (560, 840), (560, 240), TERM, TERM), line(4, (600, 300), (600, 780), TERM, TERM),
        line(5, (800, 300), (800, 780), REFL, PASS),
        line(6, (0, 0), (1920, 0), PASS, PASS), line(7, (1920, 0), (1920, 1080), PASS, PASS),
        line(8, (1920, 1080), (0, 1080), PASS, PASS), line(9, (0, 1080), (0, 0), PASS, PASS),
    ]


INV_ARC = {"id": 5, "line": False, "start": [1000.0, 340.0], "end": [1000.0, 740.0], "via": [1130.0, 540.0],
           "full": False, "left": INV, "right": PASS}
INV_CIRCLE = {"id": 1, "line": False, "start": [700, 0], "end": [400, -150], "via": [400, 150],
              "full": True, "circle": {"center": [500, 0], "radius": 200}, "left": INV, "right": INV}


def main():
    mp.mp.dps = 80
    fx = []
    mw = mirror_and_wall()
    player = (1306.67, 540.0)
    # --- R1: a line mirror. The four captures: the cursor beyond the mirror,
    # in front of or behind the player's image; the aim is at the mirror,
    # the reflected beam wraps (unplanned, or consumed by [5 L, inf])
    for tag, cursor in (("front", (480.5005, 524.9722)), ("behind", (166.1731, 535.9927))):
        p = pair(fx, "mw_%s_image" % tag, mw, player, cursor, [(5, "LEFT")])
        ks = [e["kind"] for e in p["expected"]]
        assert ks[0] == "hit" and p["expected"][0]["applied"] and ks.index("infinity") < ks.index("cursor"), kinds(p)
        assert not p["expected"][ks.index("infinity")]["applied"]
    for tag, cursor in (("front", (487.5078, 548.0149)), ("behind", (145.1512, 541.002))):
        p = pair(fx, "mw_%s_image_infinity" % tag, mw, player, cursor, [(5, "LEFT"), INF])
        ks = [e["kind"] for e in p["expected"]]
        assert ks[0] == "hit" and p["expected"][ks.index("infinity")]["applied"], kinds(p)
    # the cursor on the player's side with the wrap planned: passed with the
    # entry pending; Stage 131 (no loop detection): the planned walk meets it on
    # a later loop, the physical one ends at the wall before it
    p = pair(fx, "mw_direct_side_infinity_pending", mw, player, (1000.0, 600.0), [(5, "LEFT"), INF], expect_reach=True)
    assert [e["kind"] for e in p["expected"]][0] == "hit" and p["expected"][0]["applied"], kinds(p)
    assert not reaches(fx[-1]), kinds(fx[-1])
    pair(fx, "mw_direct_side_unplanned", mw, player, (1000.0, 600.0), [(5, "LEFT")])
    # [inf, 5 L] from the reflective side: the aim is at the mirror; the planned
    # walk passes it (the wrap is next), the physical reflects: a divergence
    # (Stage 131: the planned walk reaches the cursor on a later loop; the physical one never)
    p = pair(fx, "mw_infinity_before_mirror", mw, player, (480.5005, 524.9722), [INF, (5, "LEFT")], expect_reach=True)
    assert not reaches(fx[-1]), kinds(fx[-1])
    assert p["expected"][0]["surface_id"] == 5 and not p["expected"][0]["applied"], kinds(p)
    assert fx[-1]["expected"][0]["surface_id"] == 5 and fx[-1]["expected"][0]["applied"], kinds(fx[-1])
    # the mirror seen from its pass side: the only realization goes away,
    # wraps and meets the reflective face (the wrap unplanned, or consumed)
    m = line(1, (200, -300), (200, 300), REFL, PASS)
    for tag, plan in (("unplanned", [(1, "LEFT")]), ("planned", [INF, (1, "LEFT")])):
        for ctag, cursor in (("ahead", (600.0, 50.0)), ("behind", (-300.0, 20.0))):
            p = pair(fx, "pass_side_mirror_%s_wrap_%s" % (tag, ctag), [m], (0.0, 0.0), cursor, plan)
            ks = [e["kind"] for e in p["expected"]]
            assert ks[:2] == ["infinity", "hit"] and p["expected"][1]["applied"], kinds(p)
            assert p["expected"][0]["applied"] == (len(plan) == 2)
    # --- R2: an inversion arc seen from outside, the cursor inside: both
    # orientations fire the entry; the aim is at the arc whether the cursor's
    # image lies ahead of the player or far behind; [inf, 5 L] the long way
    arc = INV_ARC
    for tag, cursor in (("ahead", (1100.0, 560.0)), ("behind", (1010.0, 540.0))):
        p = pair(fx, "inversion_arc_image_%s" % tag, [arc], (1500.0, 540.0), cursor, [(5, "LEFT")])
        assert p["expected"][0]["surface_id"] == 5 and p["expected"][0]["applied"], kinds(p)
    p = pair(fx, "inversion_arc_infinity_first", [arc], (1500.0, 540.0), (1010.0, 540.0), [INF, (5, "LEFT")])
    assert [e["kind"] for e in p["expected"]][:2] == ["infinity", "hit"] and p["expected"][0]["applied"], kinds(p)
    # --- R3: the player inside a circle: both orientations exit it before Q0;
    # the entry alone goes toward the cursor's image, with the infinity entry
    # the other way round (the center passage before the cursor)
    circ = INV_CIRCLE
    side = "RIGHT"      # the circle's inside
    a = pair(fx, "inside_circle_entry", [circ], (500.0, 50.0), (550.0, 0.0), [(1, side)])
    b = pair(fx, "inside_circle_entry_then_infinity", [circ], (500.0, 50.0), (550.0, 0.0), [(1, side), INF])
    for p in (a, b):
        assert p["expected"][0]["surface_id"] == 1 and p["expected"][0]["applied"], kinds(p)
    sc = T.scene_from_json([circ])
    wa = T.planned(sc, (500.0, 50.0), (550.0, 0.0), [(1, side)])
    wb = T.planned(sc, (500.0, 50.0), (550.0, 0.0), [(1, side), INF])
    assert abs(wa.events[0].beam.tau + wb.events[0].beam.tau) < mp.mpf(10) ** -40, "the other way round"
    out = os.path.join(ROOT, "bench", "fixtures", "stage111_orientation.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(p["surface_id"], p["side"]) for p in f["plan"]],
              [(k[0][:3], k[1], k[3]) for k in kinds(f)][:7], reaches(f))


if __name__ == "__main__":
    main()
