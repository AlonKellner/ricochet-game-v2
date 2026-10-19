"""Stage 114: the fork rule (GAME_SPEC 13.3) -- of a fork's candidates those on
the segment are preferred, then the click-nearest -- and K15, a beam through an
inversion's centre."""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
import visibility as V  # noqa: E402
from geometry import P  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def _wall(i, s, e):
    return {"id": i, "start": list(s), "end": list(e), "via": [(s[0] + e[0]) / 2.0, (s[1] + e[1]) / 2.0], "full": False, "line": True,
            "circle": None, "left": {"type": "terminal", "interactive": False}, "right": {"type": "terminal", "interactive": False},
            "is_target": False, "player_solid": True}


def _arc(i, s, e, v, kind="circle_normal_projection", back=False):
    side = {"type": kind, "back": back, "interactive": True}
    return {"id": i, "start": list(s), "end": list(e), "via": list(v), "full": False, "line": False, "circle": None,
            "left": dict(side), "right": dict(side), "is_target": False, "player_solid": False}


def _room(arcs):
    walls = [_wall(1, (560, 240), (1360, 240)), _wall(2, (1360, 240), (1360, 840)), _wall(3, (1360, 840), (560, 840)),
             _wall(4, (560, 840), (560, 240))]
    return T.scene_from_json(walls + arcs)


# the circle centred (960, 540), r 100
C = (960.0, 540.0)
SHORT = _arc(5, (1060.0, 540.0), (960.0, 640.0), (1030.710678118655, 610.710678118655))   # a quarter: angles 0..90
LONG = _arc(5, (1060.0, 540.0), (960.0, 640.0), (860.0, 540.0))                            # three quarters: 0..180..90
FULL = {"id": 5, "start": [1060.0, 540.0], "end": [910.0, 626.6025403784438], "via": [910.0, 453.3974596215561], "full": True, "line": False,
        "circle": {"center": [960.0, 540.0], "radius": 100.0},
        "left": {"type": "circle_normal_projection", "back": False, "interactive": True},
        "right": {"type": "circle_normal_projection", "back": False, "interactive": True}, "is_target": False, "player_solid": False}


def _hit(sc, cursor, click=None):
    s = {x.id: x for x in sc}[5]
    return T.back_propagate(s, "LEFT", P(cursor), None, False, click)


def _quadrant(h):
    return (h.real > C[0], h.imag > C[1])


def test_an_arc_under_a_semicircle_ignores_the_click():
    # SHORT: the quarter from angle 0 through 45 to 90 (y down)
    sc = _room([SHORT])
    # the radial at ~14 / ~194 degrees: 14 on the quarter, whatever the click
    for click in (None, (1050.0, 560.0), (860.0, 520.0), (900.0, 460.0)):
        assert _quadrant(_hit(sc, (1200.0, 600.0), click)) == (True, True), click
    # the radial at 247 / 67 degrees: 67 (below right, y down) on the quarter
    assert _quadrant(_hit(sc, (900.0, 400.0), (860.0, 520.0))) == (True, True)
    # the radial at 135 / 315 degrees: neither on the quarter: the click's own, off segment
    assert _quadrant(_hit(sc, (900.0, 600.0), (1030.0, 470.0))) == (True, False)
    assert _quadrant(_hit(sc, (900.0, 600.0), (890.0, 610.0))) == (False, True)


def test_an_arc_over_a_semicircle_forks_only_where_both_antipodes_are_on_it():
    # LONG: from angle 0 the long way round through 270 and 180 to 90: the gap is the open quarter 0..90
    sc = _room([LONG])
    s = {x.id: x for x in sc}[5]
    from geometry import on_segment
    assert on_segment(s.seg, P((889.289321881345, 469.289321881345))) and not on_segment(s.seg, P((1030.710678118655, 610.710678118655)))
    # the radial at 45 / 225 degrees: 45 is in the gap: 225 wins, even with the click near 45
    assert _quadrant(_hit(sc, (1100.0, 680.0), (1030.0, 610.0))) == (False, False)
    assert _quadrant(_hit(sc, (1100.0, 680.0), None)) == (False, False)
    # the radial at 135 / 315 degrees: both on the arc: the click decides
    assert _quadrant(_hit(sc, (860.0, 640.0), (890.0, 610.0))) == (False, True)
    assert _quadrant(_hit(sc, (860.0, 640.0), (1030.0, 470.0))) == (True, False)


def test_a_full_circle_keeps_the_click():
    sc = _room([FULL])
    a = _hit(sc, (1100.0, 680.0), (1030.0, 610.0))
    b = _hit(sc, (1100.0, 680.0), (890.0, 470.0))
    assert a.real > C[0] and b.real < C[0]


def test_the_cursor_on_the_segment_is_its_own_candidate_and_reached_at_once():
    sc = _room([FULL])
    player = (1300.0, 540.0)
    cursor = (1060.0, 540.0)      # on the circle, angle 0
    s = {x.id: x for x in sc}[5]
    # the side the player faces (Seg.full_circle's orientation): the one hit first from outside
    side = T.physical(sc, player, (900.0, 540.0), []).events[0].side
    plan = [(5, side)]
    on = {0: (1070.0, 540.0)}
    assert T.back_propagate(s, side, P(cursor), None, True, on[0]) == P(cursor)
    pl = T.planned(sc, player, cursor, plan, None, on)
    kinds = [(e.kind, e.surface_id, e.applied) for e in pl.events]
    assert kinds[:2] == [("hit", 5, True), ("cursor", -1, False)], kinds
    assert V.visible(sc, player, cursor, plan, None, on) is True
    # the click nearer the antipode: the other candidate (the realization then decides the path)
    assert abs(T.back_propagate(s, side, P(cursor), None, True, (850.0, 540.0)) - P(cursor)) > 100


def test_k15_the_inverted_exit_beam_through_the_centre_is_straight():
    with open(os.path.join(ROOT, "bench", "scenes", "playground__projective_concentric.surfaces.json")) as f:
        sc = T.scene_from_json(json.load(f)["surfaces"])
    player = (1100.0, 700.0)
    cursor = (960.0, 840.0)
    plan = [(7, "LEFT"), (6, "LEFT"), (5, "LEFT")]
    pl = T.planned(sc, player, cursor, plan)
    fired = [e for e in pl.events if e.kind == "hit" and e.surface_id == 6 and e.applied]
    assert fired, [(e.kind, e.surface_id, e.applied) for e in pl.events]
    after = pl.events[pl.events.index(fired[0]) + 1]
    assert after.beam.kappa == 0, after.beam.kappa
    assert all(e.point is None or abs(e.point) < 1e6 for e in pl.events)
    assert V.visible(sc, player, cursor, plan) is False
