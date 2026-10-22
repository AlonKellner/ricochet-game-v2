"""Stage 101 fixtures: infinity as a point, and the pre-transform (GAME_SPEC
4.2, 6.3, 9.5, 13.1), traced by the oracle (trace2). The frame's image of
infinity Q = F(inf) is tracked along the walk; a beam passing a finite Q emits
the "infinity" event with that point; a scene with "pre_transform" surfaces
(full inversion circles, never hit) starts in the frame they define: the
initial beam is the circle through the player, the aim point and Q0.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage101.py
Writes bench/fixtures/stage101_infinity_point.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import INV, PASS, REFL, line  # noqa: E402
from gen_fixtures_stage85 import circle, fixture, kinds  # noqa: E402
from gen_fixtures_stage88 import INF, infinities, pair, pair_search, reaches  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
INV_OFF = {"type": "inversion", "interactive": False}


def pre(sid, center, radius):
    """A pre-transform inversion circle: never hit, defines the start frame."""
    d = circle(sid, center, radius, INV_OFF, INV_OFF)
    d["pre_transform"] = True
    return d


def inf_points(f):
    """The points of the infinity events (None for the real infinity)."""
    return [(e.get("x"), e.get("y")) if "x" in e else None for e in f["expected"] if e["kind"] == "infinity"]


def near(pt, x, y):
    return pt is not None and abs(mp.mpf(pt[0]) - x) < mp.mpf("1e-30") and abs(mp.mpf(pt[1]) - y) < mp.mpf("1e-30")


def main():
    mp.mp.dps = 80
    assert T.EMIT_INFINITY
    fx = []
    Q0 = pre(9, (500.0, 0.0), 200.0)          # Q0 = (500, 0), on screen
    # --- the pre-transform frame, direction traces ---
    f = fixture("pre_straight_through_q", [Q0], (0.0, 0.0), direction=(1.0, 0.0))
    # (Stage 131: no loop detection -- each loop passes Q0 and the ray's origin,
    # both significant: ten, then a max)
    loops = ["infinity", "origin"] * (T.MAX_SIGNIFICANT // 2) + ["max"]
    assert [e["kind"] for e in f["expected"]] == loops and near(inf_points(f)[0], 500, 0), kinds(f)
    fx.append(f)
    f = fixture("pre_circle_passes_q", [Q0], (0.0, 0.0), direction=(1.0, 0.5))
    assert [e["kind"] for e in f["expected"]] == loops and near(inf_points(f)[0], 500, 0), kinds(f)
    fx.append(f)
    f = fixture("pre_player_at_q_straight", [Q0], (500.0, 0.0), direction=(1.0, 0.0))
    # (its passages of Q0 unseen: it starts at Q0, the ray's origin -- each loop is one passage of the origin)
    assert [e["kind"] for e in f["expected"]] == ["origin"] * T.MAX_SIGNIFICANT + ["max"], kinds(f)
    fx.append(f)
    # two pre-transform circles: Q0 is the second inversion's image of the first center
    two = [pre(9, (500.0, 0.0), 200.0), pre(10, (800.0, 100.0), 150.0)]
    q2 = T.pre_q(T.scene_from_json(two))
    f = fixture("pre_two_inversions_q", two, (0.0, 0.0), direction=(1.0, 0.2))
    assert [e["kind"] for e in f["expected"]][0] == "infinity" and near(inf_points(f)[0], q2.real, q2.imag), kinds(f)
    fx.append(f)
    # the player at Q: the straight line to the cursor (infinity first cannot pass Q, the start)
    p = pair(fx, "pre_player_at_q_direct", [Q0], (500.0, 0.0), (300.0, 100.0), [])
    assert [e["kind"] for e in p["expected"]] == ["cursor"] + ["origin"] * T.MAX_SIGNIFICANT + ["max"], kinds(p)   # (Stage 131: from Q0, the origin, its passages of Q0 unseen)
    f = fixture("pre_player_at_q_infinity_first_planned", [Q0], (500.0, 0.0), plan=[INF], mode="planned", cursor=(300.0, 100.0))
    assert not reaches(f), kinds(f)
    fx.append(f)
    # the aim point at Q: the line through both, the cursor first at the tie
    p = pair(fx, "pre_cursor_at_q", [Q0], (0.0, 0.0), (500.0, 0.0), [])
    ks = [e["kind"] for e in p["expected"]]   # (Stage 131: the loops after, to a max)
    assert ks[:2] == ["cursor", "infinity"] and ks[-1] == "max", kinds(p)
    # --- the two realizations in the pre-transform frame ---
    p = pair(fx, "pre_direct", [Q0], (0.0, 0.0), (300.0, 100.0), [])
    ks = [e["kind"] for e in p["expected"]]
    assert ks[:2] == ["cursor", "infinity"] and ks[-1] == "max" and infinities(p)[0] is False, kinds(p)
    p = pair(fx, "pre_infinity_first", [Q0], (0.0, 0.0), (300.0, 100.0), [INF])
    ks = [e["kind"] for e in p["expected"]]
    assert ks[:2] == ["infinity", "cursor"] and ks[-1] == "max" and infinities(p)[0] is True, kinds(p)
    # --- a mirror in the pre-transform frame: Q moves with the reflection ---
    m = line(1, (400.0, -300.0), (400.0, 300.0), REFL, REFL)
    f = fixture("pre_mirror_moves_q", [Q0, m], (0.0, 0.0), direction=(1.0, 0.3))
    ks = [e["kind"] for e in f["expected"]]
    assert ks[0] == "hit" and f["expected"][0]["applied"] and near(inf_points(f)[0], 300, 0), kinds(f)
    fx.append(f)
    p = pair_search(fx, "pre_mirror_then_infinity", [Q0, m], (0.0, 0.0), (200.0, 150.0), [1, INF])
    assert True in infinities(p), kinds(p)
    p = pair_search(fx, "pre_infinity_then_mirror", [Q0, m], (0.0, 0.0), (200.0, 150.0), [INF, 1])
    assert infinities(p)[0] is True, kinds(p)
    # --- Q on a mirror: the hit fires first (a tie: hits before the passage); the
    # reflected beam departs at Q (no passage at the departure point) and, a
    # straight beam with a finite Q, passes the real infinity silently to re-hit
    # the mirror from the other side, loop (the departure re-hit) ---
    m5 = line(1, (500.0, -300.0), (500.0, 300.0), REFL, REFL)
    f = fixture("pre_q_on_mirror_tie", [Q0, m5], (0.0, 0.0), direction=(1.0, 0.0))
    ks = [e["kind"] for e in f["expected"]]
    assert ks[0] == "hit" and f["expected"][0]["applied"] and "infinity" not in ks and ks[-1] == "max", kinds(f)
    fx.append(f)
    # --- the MAX_HITS cut at a finite Q carries its point ---
    # (Stage 131: MAX_HITS lines all before Q at x = 500, so that the next event is the passage of Q)
    lines = [line(i + 1, (7.0 * (i + 1), -500.0), (7.0 * (i + 1), 500.0), PASS, PASS) for i in range(T.MAX_HITS)]
    f = fixture("pre_max_at_q_has_point", [Q0] + lines, (0.0, 0.0), direction=(1.0, 0.0))
    last = f["expected"][-1]
    assert last["kind"] == "max" and last["surface_id"] == T.INFINITY_ID and near((last["x"], last["y"]), 500, 0), last
    fx.append(f)
    # --- a projective resets the frame's infinity: the exit beam is straight, its wrap is the real one ---
    proj = {"type": "line_normal_projection", "interactive": True, "back": False}
    ln = line(1, (200.0, -400.0), (200.0, 400.0), proj, proj)
    f = fixture("pre_projective_resets_q", [Q0, ln], (0.0, 0.0), direction=(1.0, 0.2))
    ks = [(e["kind"], "x" in e) for e in f["expected"]]
    assert ks[0] == ("hit", True) and ("infinity", False) in ks, ks
    fx.append(f)
    # --- a normal world: infinity after an inversion is the center passage (Stage 101) ---
    inv = circle(1, (500.0, 0.0), 200.0, INV, INV)
    p = pair_search(fx, "inversion_then_infinity_long_way", [inv], (0.0, 0.0), (600.0, 30.0), [1, INF])
    i_inf = next(i for i, e in enumerate(p["expected"]) if e["kind"] == "infinity" and e["applied"])
    assert near((p["expected"][i_inf]["x"], p["expected"][i_inf]["y"]), 500, 0), kinds(p)
    assert i_inf < [e["kind"] for e in p["expected"]].index("cursor"), kinds(p)
    # the direct realization passes the center after the cursor, unapplied
    p = pair_search(fx, "inversion_direct_center_after_cursor", [inv], (0.0, 0.0), (600.0, 30.0), [1])
    ks = [e["kind"] for e in p["expected"]]
    assert ks.index("cursor") < ks.index("infinity"), kinds(p)
    out = os.path.join(ROOT, "bench", "fixtures", "stage101_infinity_point.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(q["surface_id"], q["side"]) for q in f["plan"]],
              [(k[0][:3], k[1], k[3]) for k in kinds(f)][:7], reaches(f))


if __name__ == "__main__":
    main()
