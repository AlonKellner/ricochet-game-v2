"""Stage 206 fixtures: counterpart surfaces (GAME_SPEC 12.1, 4.3, the user).

Usage:
  docs/research/.venv/bin/python -I docs/research/oracle/gen_fixtures_stage206.py

Writes bench/fixtures/stage206_counterpart.json: where the planned walk's beam
cannot reach its next entry's surface before the frame's infinity, it meets the
surface's counterpart -- the circle about the ray's origin with the tangent
length as radius, a line's mirror image through the origin, both mapped by the
frame -- a significant "counterpart" event that consumes nothing and marks the
plan missed; a planned terminal's ends the walk. PHYSICAL walks compute none.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import REFL, TERM, line  # noqa: E402
from gen_fixtures_stage85 import circle, fixture  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
INV = {"type": "inversion", "interactive": True}
INF = T.INFINITY_ID
O = (0.0, 0.0)
EAST = (1.0, 0.0)


def kinds(f):
    return [e["kind"] for e in f["expected"]]


def pt(e):
    return mp.mpc(mp.mpf(e["x"]), mp.mpf(e["y"]))


def side_toward(surfs, sid, target):
    """The side of surface sid a ray from the origin toward `target` meets first."""
    sc = T.scene_from_json(surfs)
    for e in T.trace_physical_direction(sc, O, mp.mpc(*target)).events:
        if e.kind == "hit" and e.surface_id == sid:
            return e.side
    raise AssertionError("no hit of %d" % sid)


def near(a, b, tol="1e-30"):
    return abs(a - b) < mp.mpf(tol)



def main():
    mp.mp.dps = 80
    fx = []
    # the planned circle: a mirror the aim east misses (its tangent cone is above the aim)
    missed = circle(1, (500, 300), 16, REFL, REFL)
    side = side_toward([missed], 1, (500, 300))
    tlen = mp.sqrt(mp.mpf(500) ** 2 + 300 ** 2 - 16 ** 2)
    for loop in (False, True):
        f = fixture("circle_mirror_outside_cone_" + ("on" if loop else "off"), [missed], O, plan=[(1, side)],
                    mode="planned", direction=EAST, infinity_loop=loop)
        assert kinds(f)[:2] == (["counterpart", "max"] if not loop else ["counterpart", "infinity"]), kinds(f)
        assert near(pt(f["expected"][0]), mp.mpc(tlen, 0)), f["expected"][0]
        assert not f["expected"][0]["applied"] and not f["expected"][0]["on_segment"]
        fx.append(f)
        # PHYSICAL: no counterpart ever (the shot is as it was)
        f = fixture("circle_mirror_outside_cone_physical_" + ("on" if loop else "off"), [missed], O, plan=[(1, side)],
                    mode="physical", direction=EAST, infinity_loop=loop)
        assert "counterpart" not in kinds(f), kinds(f)
        fx.append(f)
    # exact tangency: the aim's exact line touches the circle -- no crossing (exact); the
    # counterpart point is the tangent point exactly
    tang = circle(1, (400, 300), 300, REFL, REFL)
    f = fixture("circle_mirror_exact_tangent", [tang], O, plan=[(1, side_toward([tang], 1, (400, 300)))],
                mode="planned", direction=EAST, infinity_loop=False)
    assert kinds(f) == ["counterpart", "max"] and near(pt(f["expected"][0]), mp.mpc(400, 0)), (kinds(f), f["expected"][0])
    fx.append(f)
    # a grazing aim inside the cone hits: no counterpart (continuity across the edge is the point's)
    f = fixture("circle_mirror_grazing_hit", [missed], O, plan=[(1, side)], mode="planned",
                direction=(1.0, 0.5648), infinity_loop=False)
    assert kinds(f)[0] == "hit" and "counterpart" not in kinds(f), kinds(f)
    fx.append(f)
    # the circle behind the aim: the counterpart ahead at the tangent length, both loop modes
    behind = circle(1, (-500, 0), 16, REFL, REFL)
    bside = side_toward([behind], 1, (-500, 0))
    for loop in (False, True):
        f = fixture("circle_behind_" + ("on" if loop else "off"), [behind], O, plan=[(1, bside)], mode="planned",
                    direction=EAST, infinity_loop=loop)
        assert kinds(f)[0] == "counterpart" and near(pt(f["expected"][0]), mp.mpc(mp.sqrt(mp.mpf(500) ** 2 - 256), 0)), kinds(f)
        fx.append(f)
    # a planned line the aim faces away from: the mirror image through the origin, both modes
    away = line(1, (-100, -500), (-100, 500), REFL, REFL)
    aside = side_toward([away], 1, (-100, 0))
    for loop in (False, True):
        f = fixture("line_facing_away_" + ("on" if loop else "off"), [away], O, plan=[(1, aside)], mode="planned",
                    direction=EAST, infinity_loop=loop)
        assert kinds(f)[0] == "counterpart" and near(pt(f["expected"][0]), mp.mpc(100, 0)), (kinds(f), f["expected"][0])
        fx.append(f)
    # a planned line parallel to the aim: no counterpart (none would be met either)
    par = line(1, (-500, 50), (500, 50), REFL, REFL)
    f = fixture("line_parallel", [par], O, plan=[(1, "LEFT")], mode="planned", direction=EAST, infinity_loop=False)
    assert kinds(f) == ["max"], kinds(f)
    fx.append(f)
    # infinity planned first: after the wrap the counterpart's far crossing, on the way back
    f = fixture("inf_then_circle_on", [missed], O, plan=[(INF, ""), (1, side)], mode="planned", direction=EAST,
                infinity_loop=True)
    assert kinds(f)[:2] == ["infinity", "counterpart"] and near(pt(f["expected"][1]), mp.mpc(-tlen, 0)), (kinds(f), f["expected"][1])
    fx.append(f)
    # a planned terminal the aim misses, the cursor before it (Stage 201): the planned walk ends at the
    # terminal's counterpart; the physical walk goes on to the wall
    ring = circle(1, (200, 60), 16, TERM, TERM)
    wall = line(9, (400, -500), (400, 500), TERM, TERM)
    rside = side_toward([ring], 1, (200, 60))
    f = fixture("terminal_missed_cursor_before_planned", [ring, wall], O, plan=[(1, rside)], mode="planned", cursor=(100.0, 0.0))
    assert kinds(f) == ["cursor", "counterpart"], kinds(f)
    assert near(pt(f["expected"][1]), mp.mpc(mp.sqrt(mp.mpf(200) ** 2 + 60 ** 2 - 256), 0)), f["expected"][1]
    fx.append(f)
    f = fixture("terminal_missed_cursor_before_physical", [ring, wall], O, plan=[(1, rside)], mode="physical", cursor=(100.0, 0.0))
    assert kinds(f) == ["cursor", "hit"], kinds(f)
    fx.append(f)
    # a wall before the counterpart point: the planned walk computes its entries alone (the
    # counterpart), the physical walk stops at the wall
    near_wall = line(9, (300, -500), (300, 500), TERM, TERM)
    f = fixture("wall_before_counterpart_planned", [missed, near_wall], O, plan=[(1, side)], mode="planned",
                direction=EAST, infinity_loop=False)
    assert kinds(f) == ["counterpart", "max"], kinds(f)
    fx.append(f)
    f = fixture("wall_before_counterpart_physical", [missed, near_wall], O, plan=[(1, side)], mode="physical",
                direction=EAST, infinity_loop=False)
    assert kinds(f) == ["hit"], kinds(f)
    fx.append(f)
    # the ray's origin inside the planned circle: every aim crosses it, no counterpart
    around = circle(1, (100, 0), 300, REFL, REFL)
    f = fixture("origin_inside_planned_circle", [around], O, plan=[(1, side_toward([around], 1, (400, 0)))],
                mode="planned", direction=EAST, infinity_loop=False)
    assert kinds(f)[0] == "hit" and "counterpart" not in kinds(f), kinds(f)
    fx.append(f)
    # an inverted frame: the aim east is inverted in a ring; the inverted beam (a circle through
    # the ring's centre Q) misses a small circle inside: the counterpart is the frame's -- in the
    # frame (the inversion about Q through the hit point p) its point lies at the tangent length
    # from p
    ringinv = circle(1, (200, 30), 100, INV, INV)
    small = circle(2, (150, 60), 10, REFL, REFL)
    iside = side_toward([ringinv], 1, (200, 30))
    sc = T.scene_from_json([ringinv, small])
    probe = T.trace_mode_direction(sc, O, mp.mpc(*EAST), [(1, iside)], None, T.PLANNED)
    inv = [e for e in probe.events if e.kind == "hit" and e.applied]
    assert inv and inv[0].surface_id == 1
    b1 = probe.events[1].beam     # the inverted beam (the event after the inversion lies on it)
    pin = inv[0].point
    Q = mp.mpc(200, 30)
    R2 = abs(pin - Q) ** 2
    # S' = I(S): the small circle inverted about Q through p
    c1, rr1 = T._invert_circle(mp.mpc(150, 60), mp.mpf(100), Q, R2)
    tlen2 = abs(pin - c1) ** 2 - rr1
    assert tlen2 > 0
    sside = "LEFT"
    for s in ("LEFT", "RIGHT"):
        w = T.trace_mode_direction(sc, O, mp.mpc(*EAST), [(1, iside), (2, s)], None, T.PLANNED)
        if any(e.kind == "counterpart" for e in w.events):
            sside = s
            break
    for loop in (False, True):
        f = fixture("inverted_frame_" + ("on" if loop else "off"), [ringinv, small], O, plan=[(1, iside), (2, sside)],
                    mode="planned", direction=EAST, infinity_loop=loop)
        k = kinds(f)
        assert k[0] == "hit" and k[1] == "counterpart", k
        cp = pt(f["expected"][1])
        # on the inverted beam, and in the frame at the tangent length from p
        assert near(b1.at(b1.param_of(cp)), cp, "1e-20"), cp
        icp = Q + (cp - Q) * (R2 / abs(cp - Q) ** 2)
        assert near(abs(icp - pin) ** 2, tlen2, "1e-20"), (abs(icp - pin) ** 2, tlen2)
        fx.append(f)
    # the symmetric mirrors (the corpus's three_mirrors): the far mirror's image through a hit on the middle one is
    # the near one exactly -- the counterpart shares its carrier's roots, so the near mirror's crossing (planned later,
    # an event) and the counterpart tie exactly, the id first: the same at every precision
    near_m = line(1, (800, -500), (800, 500), REFL, REFL)
    mid = line(2, (1000, -500), (1000, 500), REFL, REFL)
    far = line(3, (1200, -500), (1200, 500), REFL, REFL)
    three = [near_m, mid, far]
    mside = side_toward([mid], 2, (1000, 0))
    f = fixture("symmetric_mirrors_tie", three, (900.0, 0.0), plan=[(2, mside), (3, "LEFT"), (1, "LEFT")], mode="planned",
                direction=(1.0, 0.25), infinity_loop=False)
    k = kinds(f)
    assert k[:3] == ["hit", "hit", "counterpart"] and f["expected"][0]["applied"] and f["expected"][1]["surface_id"] == 1 \
        and f["expected"][2]["surface_id"] == 3, k
    assert near(pt(f["expected"][1]), pt(f["expected"][2])), (f["expected"][1], f["expected"][2])
    fx.append(f)
    path = os.path.join(ROOT, "bench", "fixtures", "stage206_counterpart.json")
    with open(path, "w") as out:
        json.dump({"dps": mp.mp.dps, "fixtures": fx}, out, indent=1)
        out.write("\n")
    print(path, len(fx), "fixtures")


if __name__ == "__main__":
    main()
