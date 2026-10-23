"""Stage 113: the visibility region through projective plan entries (GAME_SPEC
15, docs/TDD_18_PROJECTIVE_VISIBILITY.md). Two solver findings the region's
exact-data members exposed:

K11: a projective's planned hit point that is a data point of its segment (an
endpoint, the via) is that point exactly -- decided by an exact predicate on
the data (_exact_data_hit), never by the rounding of c + R u. At an endpoint
the physical hit is blocked on one side only, so it does not fire, while the
planned walk fires the entry there: such a cursor is not visible.

K12: a cursor exactly at a projective's departure point (the cursor on a line
projective's carrier: its own foot) is reached at once, on the exit beam.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
import visibility as V  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
mp.mp.dps = 50


def _scene(name):
    path = os.path.join(ROOT, "bench", "scenes", name + ".surfaces.json")
    return T.scene_from_json(json.load(open(path))["surfaces"])


def _hits(w):
    return [(e.kind, e.surface_id, e.applied, None if e.point is None else (e.point.real, e.point.imag)) for e in w.events]


def test_k11_the_hit_point_at_an_arc_endpoint_is_the_endpoint_exactly():
    # playground__projective: arc 9 is the right half of the circle centred
    # (700, 500), r 250, from (850, 300) to (850, 700). Its endpoint's radius
    # (0.6, -0.8) is not representable, so c + R u lands off the endpoint by
    # arithmetic alone; the exact predicate says the hit point is the endpoint.
    sc = _scene("playground__projective")
    player = (1000.0, 150.0)
    cursor = (940.0, 180.0)              # c + 400 (0.6, -0.8): on the radial through the endpoint
    plan = [(9, "LEFT")]
    hits = T.projective_hits(sc, plan, cursor)
    H, line, D = hits[0]
    assert D is not None and D == mp.mpc(850, 300) and H == D
    assert line is not None
    pl = T.planned(sc, player, cursor, plan)
    ph = T.physical(sc, player, cursor, plan)
    # both walks reach the endpoint exactly (after two carriers crossed off their
    # segments); the planned entry fires there, the physical hit is blocked on
    # one side only
    on9 = lambda w: next(h for h in _hits(w) if h[1] == 9)  # noqa: E731
    assert on9(pl) == ("hit", 9, True, (850, 300)), _hits(pl)
    assert on9(ph) == ("hit", 9, False, (850, 300)), _hits(ph)
    assert V.visible(sc, player, cursor, plan) is False
    # a control just off the radial: an interior hit point, fired by both
    control = (940.0, 183.0)
    assert T.projective_hits(sc, plan, control)[0][2] is None
    assert on9(T.physical(sc, player, control, plan))[:3] == ("hit", 9, True)


def test_k11_the_exact_image_runs_through_an_exact_hit_point():
    sc = _scene("playground__projective")
    cursor = (940.0, 180.0)
    assert T.exact_image(sc, cursor, [(9, "LEFT")]) == (T.F(850.0), T.F(300.0))
    assert T.exact_image(sc, (940.0, 183.0), [(9, "LEFT")]) is None


def test_k11_a_line_normal_hit_point_at_the_via_is_exact():
    # projective_lines: 5 is the vertical line x = 750, y 300..780, via (750, 540)
    sc = _scene("playground__projective_lines")
    hits = T.projective_hits(sc, [(5, "LEFT")], (900.0, 540.0))
    assert hits[0][2] == mp.mpc(750, 540) and hits[0][1] is not None
    # (Stage 125: off the via, the foot (750, 541) is exact data too -- floats exactly)
    assert T.projective_hits(sc, [(5, "LEFT")], (900.0, 541.0))[0][2] == mp.mpc(750, 541)


def test_k11_a_directional_arc_hit_point_at_its_tangency_endpoint():
    # directional_arcs: 5 is the right half of the circle centred (850, 540),
    # r 150, n = (1, 0): the top endpoint (850, 390) is where n is tangent
    sc = _scene("playground__directional_arcs")
    player = (960.0, 300.0)
    plan = [(5, "LEFT")]
    for cursor in ((1000.0, 390.0), (700.0, 390.0)):
        hits = T.projective_hits(sc, plan, cursor)
        assert hits[0][2] == mp.mpc(850, 390), cursor
        ph = T.physical(sc, player, cursor, plan)
        assert _hits(ph)[0] == ("hit", 5, False, (850, 390)), _hits(ph)
        assert V.visible(sc, player, cursor, plan) is False


def test_k12_the_cursor_at_a_line_projectives_departure_point_is_reached_at_once():
    sc = _scene("playground__projective_lines")
    player = (760.0, 400.0)
    cursor = (750.0, 540.0)
    plan = [(5, "LEFT")]
    pl = T.planned(sc, player, cursor, plan)
    assert _hits(pl)[:2] == [("hit", 5, True, (750, 540)), ("cursor", -1, False, (750, 540))], _hits(pl)
    assert V.visible(sc, player, cursor, plan) is True


def test_k14_two_projectives_fired_at_one_point_are_different_frames():
    # projective: arc 9 is centred (700, 500), r 250; line 6 (a line normal, x = 700,
    # y 200..800) runs through the centre, so the fork line of the default click
    # is line 6's own carrier. From inside the circle the planned walk fires 9
    # off-segment at the top (700, 250) and runs up the fork line; the physical
    # walk fires 6 there (on segment) and leaves horizontally: two frames, one
    # projective firing each -- not one epoch "1".
    sc = _scene("playground__projective")
    player = (676.666687, 390.0)
    plan = [(9, "RIGHT")]
    cursor = (700.0, 200.0)
    pl = T.planned(sc, player, cursor, plan)
    ph = T.physical(sc, player, cursor, plan)
    # (the crossing (700, 250) is a computed point: compare the decisions. Stage 133,
    # F21: it is exact data -- the projective's hit, floats exactly -- so line 6's
    # crossing there ties with 9's by id: passed first, then 9 fires)
    assert [h[:3] for h in _hits(pl)[:3]] == [("hit", 6, False), ("hit", 9, True), ("cursor", -1, False)], _hits(pl)
    # (physically 6 fires there first -- the tie by id -- and leaves horizontally)
    assert _hits(ph)[0][:3] == ("hit", 6, True), _hits(ph)
    assert V.visible(sc, player, cursor, plan) is False
    assert V.visible(sc, player, (700.0, 800.0), plan) is False


def test_b9_a_directional_normal_along_its_line_reaches_nothing():
    # the former directional_degenerate playground (rejected at load now): line 5
    # is x = 750, y 300..780, its normal (0, 1) along it
    with open(os.path.join(ROOT, "bench", "fixtures", "stage113_degenerate_directional.json")) as f:
        sc = T.scene_from_json(json.load(f)["surfaces"])
    player = (760.0, 400.0)
    for cursor in ((900.0, 400.0), (750.0, 200.0), (700.0, 500.0), (1300.0, 700.0)):
        assert V.visible(sc, player, cursor, [(5, "LEFT")]) is False, cursor
        assert V.visible(sc, player, cursor, [(5, "RIGHT")]) is False, cursor
