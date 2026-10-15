"""Oracle v2 trace semantics: hand-checkable sanity tests (Stage 81)."""
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from geometry import P                         # noqa: E402
import trace2 as T                             # noqa: E402

mp.mp.dps = 50


def line(sid, s, e, left=None, right=None, target=False):
    return T.surface_from_json({"id": sid, "line": True, "start": list(s), "end": list(e),
                                "via": [(s[0] + e[0]) / 2.0, (s[1] + e[1]) / 2.0], "full": False,
                                "left": left, "right": right, "is_target": target})


REFL = {"type": "reflection", "interactive": True}
TERM = {"type": "terminal", "interactive": False}
PASS = {"type": "pass", "interactive": False}


def hits(walk):
    return [(e.surface_id, e.applied, e.terminal) for e in walk.events if e.kind == "hit"]


def without_infinity(walk):
    """The events of a walk minus the infinity events (Stage 88 inserts them
    at every wrap: the pre-Stage-88 indices of a trace)."""
    return [e for e in walk.events if e.kind != "infinity"]


def test_mirror_reflects_by_angle_law():
    # vertical two-sided mirror x=100; beam from (0,0) at 45° down-right
    m = line(1, (100, -500), (100, 500), REFL, REFL)
    w = T.trace_physical_direction([m], (0, 0), mp.mpc(1, 1))
    ev = without_infinity(w)
    assert ev[0].surface_id == 1 and ev[0].applied
    assert abs(ev[0].point - mp.mpc(100, 100)) < mp.mpf(10) ** -40
    # the reflected line wraps through infinity and re-hits the mirror from the
    # other side (departure re-hit = ordinary hit): that cancels the reflection,
    # the original beam continues, wraps, re-hits... until MAX_HITS
    assert ev[1].surface_id == 1 and ev[1].rehit and ev[1].applied
    assert ev[-1].kind == "max"


def test_terminal_ends_trace():
    wall = line(1, (100, -500), (100, 500), TERM, TERM)
    w = T.trace_physical_direction([wall], (0, 0), mp.mpc(1, 0))
    assert hits(w) == [(1, True, True)]
    assert w.events[-1].terminal


def test_pass_surface_is_event_but_no_effect():
    p = line(1, (50, -500), (50, 500), PASS, PASS)
    wall = line(2, (100, -500), (100, 500), TERM, TERM)
    w = T.trace_physical_direction([p, wall], (0, 0), mp.mpc(1, 0))
    assert hits(w) == [(1, False, False), (2, True, True)]


def test_one_sided_mirror_inert_side_passes():
    # vertical mirror from (100,-500) to (100,500): moving +x the beam arrives
    # from the RIGHT side (hand computed in test_oracle_v2); LEFT-only mirror
    m = line(1, (100, -500), (100, 500), REFL, None)
    wall = line(2, (200, -500), (200, 500), TERM, TERM)
    w = T.trace_physical_direction([m, wall], (0, 0), mp.mpc(1, 0))
    assert hits(w)[0] == (1, False, False), "inert (RIGHT) side passes"
    assert hits(w)[1] == (2, True, True)


def test_off_segment_passes_in_physical():
    m = line(1, (100, 10), (100, 500), REFL, REFL)     # segment does not reach y=0
    wall = line(2, (200, -500), (200, 500), TERM, TERM)
    w = T.trace_physical_direction([m, wall], (0, 0), mp.mpc(1, 0))
    assert w.events[0].surface_id == 1 and not w.events[0].on_segment and not w.events[0].applied


def test_max_hits_32_parallel_mirrors():
    a = line(1, (0, -500), (0, 500), REFL, REFL)
    b = line(2, (100, -500), (100, 500), REFL, REFL)
    w = T.trace_physical_direction([a, b], (50, 0), mp.mpc(1, 0))
    assert sum(1 for e in w.events if e.kind == "hit") == 32
    assert w.events[-1].kind == "max"


def test_planned_applies_off_segment_entry():
    m = line(1, (100, 10), (100, 500), REFL, REFL)
    w = T.trace_mode_direction([m], (0, 0), mp.mpc(1, 0), [(1, "RIGHT")], P((0, 5)), T.PLANNED)
    assert w.events[0].surface_id == 1 and w.events[0].applied, "planned honours off-segment entry"


def test_planned_reaches_cursor_via_mirror():
    m = line(1, (100, -500), (100, 500), REFL, REFL)
    surfaces = [m]
    plan = [(1, "RIGHT")]
    w = T.planned(surfaces, (0, 0), (0, 50), plan)
    kinds = [e.kind for e in w.events]
    assert "cursor" in kinds
    c = w.events[kinds.index("cursor")]
    assert abs(c.point - mp.mpc(0, 50)) < mp.mpf(10) ** -40


def test_y_junction_fires_lower_id():
    # two mirror segments meeting at (100, 0); beam aimed exactly at the vertex
    a = line(1, (100, 0), (150, -100), REFL, REFL)
    b = line(2, (100, 0), (150, 100), REFL, REFL)
    w = T.trace_physical_direction([a, b], (0, 0), mp.mpc(1, 0))
    first = w.events[0]
    assert first.point == mp.mpc(100, 0)
    assert first.surface_id == 1 and first.applied


def test_partial_blockage_does_not_leak():
    # graze an endpoint on its open side, then cross a pass surface: no effect
    a = line(1, (100, 0), (100, 100), REFL, REFL)        # starts exactly on the ray
    p = line(2, (200, -100), (200, 100), PASS, PASS)
    w = T.trace_physical_direction([a, p], (0, 0), mp.mpc(1, 0))
    assert [(e.surface_id, e.applied) for e in w.events if e.kind == "hit"][:2] == [(1, False), (2, False)]


# ---------------------------------------------------------------- portals (Stage 82)

def portal_pair(sid, a, b, theta, d):
    """Source sid (a -> b) and its pair sid + 1 = T(source), T(z) = e^{i theta} z + d."""
    import math
    c, s = math.cos(theta), math.sin(theta)
    T = lambda p: (c * p[0] - s * p[1] + d[0], s * p[0] + c * p[1] + d[1])
    en = (c, -s)
    inv_d = [-(en[0] * d[0] - en[1] * d[1]), -(en[0] * d[1] + en[1] * d[0])]
    ca = {"type": "portal", "interactive": True, "theta": theta, "d": list(d), "pair": sid + 1}
    cb = {"type": "portal", "interactive": True, "theta": -theta, "d": inv_d, "pair": sid}
    return line(sid, a, b, ca, ca), line(sid + 1, T(a), T(b), cb, cb)


def test_portal_translates_beam():
    src, tgt = portal_pair(1, (100, -500), (100, 500), 0.0, (300, 0))
    wall = line(3, (600, -500), (600, 500), TERM, TERM)
    w = T.trace_physical_direction([src, tgt, wall], (0, 0), mp.mpc(1, 0.25))
    ev = w.events
    assert (ev[0].surface_id, ev[0].applied) == (1, True)
    assert abs(ev[0].point - mp.mpc(100, 25)) < mp.mpf(10) ** -40
    # exits at T(h) = (400, 25), same direction: the next event is the wall at
    # y = 25 + 200 * 0.25, not the paired surface at the exit point
    assert (ev[1].surface_id, ev[1].terminal) == (3, True)
    assert abs(ev[1].point - mp.mpc(600, 75)) < mp.mpf(10) ** -40


def test_portal_mirror_then_pair_pushes():
    # src -> exit at the pair, mirror, back into the pair: the top of the
    # stack is the mirror, so the pair pushes its own transform (T^-1)
    src, tgt = portal_pair(1, (100, -500), (100, 500), 0.0, (300, 0))
    m = line(3, (500, -500), (500, 500), REFL, REFL)
    wall = line(4, (-200, -500), (-200, 500), TERM, TERM)
    w = T.trace_physical_direction([src, tgt, m, wall], (0, 0), mp.mpc(1, 0.25))
    hits = [(e.surface_id, e.applied) for e in w.events if e.kind == "hit"]
    assert hits[:4] == [(1, True), (3, True), (2, True), (4, True)], hits
    assert abs(w.events[2].point - mp.mpc(400, 75)) < mp.mpf(10) ** -40
    # exits at T^-1(400, 75) = (100, 75) going -x, to the wall at y = 75 + 300/4
    assert abs(w.events[3].point - mp.mpc(-200, 150)) < mp.mpf(10) ** -40


def test_portal_pop_via_departure_rehit():
    # short portals: the exit beam wraps through infinity, misses the source
    # segment (off-segment pass) and returns to its departure point on the
    # pair (re-hit) -> pop: the original beam resumes at the source hit point
    src, tgt = portal_pair(1, (100, -10), (100, 10), 0.0, (300, 0))
    w = T.trace_physical_direction([src, tgt], (0, 0), mp.mpc(1, 0.0625))
    ev = without_infinity(w)
    got = [(e.surface_id, e.applied, e.on_segment, e.rehit) for e in ev[:4]]
    assert got == [(1, True, True, False), (1, False, False, False), (2, True, True, True), (2, False, False, False)], got
    assert abs(ev[0].point - mp.mpc(100, 6.25)) < mp.mpf(10) ** -40
    assert abs(ev[1].point - mp.mpc(100, -12.5)) < mp.mpf(10) ** -40
    assert abs(ev[2].point - mp.mpc(400, 6.25)) < mp.mpf(10) ** -40
    # restored original beam, from the source hit point onwards
    assert abs(ev[3].point - mp.mpc(400, 25)) < mp.mpf(10) ** -40


def test_portal_rotation():
    import math
    src, tgt = portal_pair(1, (100, -500), (100, 500), math.pi / 2, (0, 0))
    w = T.trace_physical_direction([src, tgt], (0, 0), mp.mpc(1, 0.25))
    ev = without_infinity(w)
    e0 = ev[0]
    assert e0.surface_id == 1 and e0.applied
    # the exit beam is the rotated beam: its point is T(h) = i h
    e1 = ev[1]
    assert e1.kind in ("hit", "escape", "max")


def test_planned_through_portal_aims_at_preimage():
    src, tgt = portal_pair(1, (100, -500), (100, 500), 0.0, (300, 0))
    img = T.aim_image([src, tgt], [(1, "RIGHT")], (500, 100))
    assert img == mp.mpc(200, 100)


# ---------------------------------------------------------------- projectives (Stage 83)

def proj(kind, back=False, normal=None):
    c = {"type": kind, "interactive": True, "back": back}
    if normal is not None:
        c["normal"] = list(normal)
    return c


def test_line_normal_projection_through_and_back():
    wall = line(9, (-1000, -1000), (-1000, 1000), TERM, TERM)
    far = line(8, (1000, -1000), (1000, 1000), TERM, TERM)
    for back in (False, True):
        p = line(1, (250, -500), (250, 500), proj("line_normal_projection", back), proj("line_normal_projection", back))
        # beam from (100, 200) towards (600, 400): crosses x = 250 at y = 260
        w = T.trace_physical_direction([p, wall, far], (100, 200), mp.mpc(500, 200))
        hits = [e for e in w.events if e.kind == "hit"]
        assert hits[0].surface_id == 1 and hits[0].applied
        assert abs(hits[0].point - mp.mpc(250, 260)) < mp.mpf(10) ** -40
        # exit is horizontal (the line normal): the next hit is at y = 260
        assert hits[1].terminal and abs(hits[1].point.imag - 260) < mp.mpf(10) ** -40
        assert (hits[1].point.real > 0) == (not back)


def test_circle_normal_projection_from_inside_exits_outward():
    a = {"id": 1, "line": False, "start": [200, 100], "end": [200, 300], "via": [300, 200], "full": False,
         "left": proj("circle_normal_projection"), "right": proj("circle_normal_projection"), "is_target": False}
    arc_s = T.surface_from_json(a)
    far = line(8, (1000, -1000), (1000, 1000), TERM, TERM)
    w = T.trace_physical_direction([arc_s, far], (250, 200), mp.mpc(1, 0))
    hits = [e for e in w.events if e.kind == "hit"]
    assert hits[0].surface_id == 1 and hits[0].applied and abs(hits[0].point - mp.mpc(300, 200)) < mp.mpf(10) ** -40
    assert abs(hits[1].point - mp.mpc(1000, 200)) < mp.mpf(10) ** -40


def test_directional_projection_fixed_normal():
    p = line(1, (300, -500), (300, 500), proj("line_directional_projection", False, (1, 1)),
             proj("line_directional_projection", False, (1, 1)))
    far = line(8, (1000, -5000), (1000, 5000), TERM, TERM)
    w = T.trace_physical_direction([p, far], (0, 0), mp.mpc(1, 0.1))
    hits = [e for e in w.events if e.kind == "hit"]
    # crossing +x: through exits along +(1,1)
    h = hits[0].point
    assert abs(hits[1].point - (h + (1000 - h.real) * mp.mpc(1, 1))) < mp.mpf(10) ** -40


def test_directional_parallel_axis_convention():
    # normal parallel to the line: axis . grad = 0 -> +axis (through)
    p = line(1, (300, -500), (300, 500), proj("line_directional_projection", False, (0, 1)),
             proj("line_directional_projection", False, (0, 1)))
    top = line(8, (-5000, 1000), (5000, 1000), TERM, TERM)
    w = T.trace_physical_direction([p, top], (0, 0), mp.mpc(1, 0))
    hits = [e for e in w.events if e.kind == "hit"]
    assert hits[1].surface_id == 8 and abs(hits[1].point - mp.mpc(300, 1000)) < mp.mpf(10) ** -40


def test_projective_clears_stack():
    m = line(1, (100, -500), (100, 500), REFL, REFL)
    p = line(2, (300, -500), (300, 500), proj("line_normal_projection"), proj("line_normal_projection"))
    w = T.trace_physical_direction([m, p], (500, 0), mp.mpc(-1, 0.25))
    hits = [e for e in w.events if e.kind == "hit"]
    assert hits[0].surface_id == 2 and hits[0].applied
    # after the projective the beam is a fresh straight line along -x; it
    # reflects on the mirror (a push on an empty stack), never a pop
    assert hits[1].surface_id == 1 and hits[1].applied
    assert abs(hits[1].point - mp.mpc(100, 50)) < mp.mpf(10) ** -40


# ---------------------------------------------------------------- Stage 87: click forks

def _circle_normal(back=False):
    cfg = {"type": "circle_normal_projection", "interactive": True, "back": back}
    return T.surface_from_json({"id": 1, "line": False, "start": [700.0, 0.0], "end": [400.0, -150.0],
                                "via": [400.0, 150.0], "full": True,
                                "circle": {"center": [500.0, 0.0], "radius": 200.0},
                                "left": cfg, "right": cfg})


def test_back_propagate_click_picks_nearest_candidate():
    s = _circle_normal()
    X = P((520.0, 30.0))                     # inside the circle: a fork
    cands = [H for H, _ in T.back_propagate_candidates(s, T.LEFT, X)]
    assert len(cands) == 2
    for H in cands:
        # both exit lines pass through X (radial lines through the center)
        assert abs(abs(H - P((500.0, 0.0))) - 200) < mp.mpf(10) ** -40
        click = (float(H.real) + 5, float(H.imag) - 5)
        assert T.back_propagate(s, T.LEFT, X, click=click) == H


def test_back_propagate_default_click_is_midpoint():
    s = _circle_normal()
    X = P((520.0, 30.0))
    cands = [H for H, _ in T.back_propagate_candidates(s, T.LEFT, X)]
    want = min(cands, key=lambda H: abs(H - P((400.0, 150.0))))
    assert T.back_propagate(s, T.LEFT, X) == want


def test_back_propagate_click_decides_outside_too():
    s = _circle_normal()
    X = P((900.0, 100.0))                    # outside: one direct, one through infinity
    c = T.back_propagate_candidates(s, T.LEFT, X)
    assert sorted(d for _, d in c) == [False, True]
    for H, _ in c:
        assert T.back_propagate(s, T.LEFT, X, click=(float(H.real), float(H.imag))) == H


def test_back_propagate_provenance_excludes_target_even_when_clicked():
    s = _circle_normal()
    X = P((500.0, 200.0))                    # on the carrier
    H = T.back_propagate(s, T.LEFT, X, x_on_carrier=True, click=(500.0, 200.0))
    assert abs(H - X) > 100


def test_line_projective_ignores_click():
    cfg = {"type": "line_normal_projection", "interactive": True, "back": False}
    s = line(1, (300, -400), (300, 400), cfg, cfg)
    X = P((600.0, 200.0))
    a = T.back_propagate(s, T.RIGHT, X, click=(300.0, -390.0))
    b = T.back_propagate(s, T.RIGHT, X, click=(300.0, 390.0))
    assert a == b == P((300.0, 200.0))


def test_cursor_event_only_on_the_planned_path():
    # the far click: the beam meets the circle earlier on the planned side
    # and fires there -- off the planned path, no cursor event (never at a
    # projection of the cursor)
    walls = [line(10 + i, a, b, TERM, TERM) for i, (a, b) in enumerate(
        [((-1000, -1000), (1000, -1000)), ((1000, -1000), (1000, 1000)),
         ((1000, 1000), (-1000, 1000)), ((-1000, 1000), (-1000, -1000))])]
    sc = [_circle_normal()] + walls
    for click, reach in (((390.0, -160.0), True), ((610.0, 165.0), False)):
        w = T.planned(sc, (100.0, -300.0), (520.0, 30.0), [(1, T.LEFT)], hints={0: click})
        cur = [e for e in w.events if e.kind == "cursor"]
        assert bool(cur) == reach
        for e in cur:
            assert abs(e.point - P((520.0, 30.0))) < mp.mpf(10) ** -30


# ---------------------------------------------------------------- Stage 88: infinity

import contextlib


@contextlib.contextmanager
def emit_infinity(on=True):
    old = T.EMIT_INFINITY
    T.EMIT_INFINITY = on
    try:
        yield
    finally:
        T.EMIT_INFINITY = old


def kinds(walk):
    return [e.kind for e in walk.events]


def test_infinity_on_by_default_and_off_reproduces_the_legacy_trace():
    assert T.EMIT_INFINITY is True          # Stage 88b (B9 approved)
    p = line(1, (50, -500), (50, 500), PASS, PASS)
    w = T.trace_physical_direction([p], (0, 0), mp.mpc(1, 0))
    assert kinds(w) == ["hit", "infinity", "escape"]
    with emit_infinity(False):
        w = T.trace_physical_direction([p], (0, 0), mp.mpc(1, 0))
        assert kinds(w) == ["hit", "escape"]


def test_infinity_event_between_ahead_and_wrapped_crossings():
    # pass lines ahead (x = 50) and behind (x = -50) of the player: the beam
    # crosses the one ahead, wraps, crosses the one behind, then escapes
    ahead = line(1, (50, -500), (50, 500), PASS, PASS)
    behind = line(2, (-50, -500), (-50, 500), PASS, PASS)
    with emit_infinity():
        w = T.trace_physical_direction([ahead, behind], (0, 0), mp.mpc(1, 0))
    assert kinds(w) == ["hit", "infinity", "hit", "escape"]
    assert [e.surface_id for e in w.events] == [1, T.INFINITY_ID, 2, -1]
    inf_ev = w.events[1]
    assert inf_ev.point is None and mp.isinf(inf_ev.t) and not inf_ev.applied
    assert w.count == 4, "the infinity event counts toward MAX_HITS"


def test_infinity_counts_toward_max_hits():
    # one two-sided mirror in an open scene: reflect, wrap, re-hit (pop), wrap,
    # ... until MAX_HITS. Every other event is now the wrap.
    m = line(1, (100, -500), (100, 500), REFL, REFL)
    with emit_infinity():
        w = T.trace_physical_direction([m], (0, 0), mp.mpc(1, 1))
    ks = kinds(w)
    assert ks[-1] == "max"
    body = ks[:-1]
    assert len(body) == T.MAX_HITS
    assert body[::2] == ["hit"] * (T.MAX_HITS // 2) and body[1::2] == ["infinity"] * (T.MAX_HITS // 2)


def test_max_hits_cut_at_the_wrap_is_a_max_at_infinity():
    # 32 pass lines ahead: the 33rd event is the wrap itself
    lines = [line(i + 1, (10 * (i + 1), -500), (10 * (i + 1), 500), PASS, PASS) for i in range(T.MAX_HITS)]
    with emit_infinity():
        w = T.trace_physical_direction(lines, (0, 0), mp.mpc(1, 0))
    assert kinds(w) == ["hit"] * T.MAX_HITS + ["max"]
    last = w.events[-1]
    assert last.point is None and last.surface_id == T.INFINITY_ID and mp.isinf(last.t)


def test_circle_beam_never_emits_infinity():
    # after an inversion the beam is a circle: its t = inf is a finite point
    inv = {"type": "inversion", "interactive": True}
    c = T.surface_from_json({"id": 1, "line": False, "start": [700, 0], "end": [400, -150], "via": [400, 150],
                             "full": True, "circle": {"center": [500, 0], "radius": 200}, "left": inv, "right": inv})
    with emit_infinity():
        w = T.trace_physical_direction([c], (0, 0), mp.mpc(1, 0.1))
    assert w.events[0].kind == "hit" and w.events[0].applied
    # the event after the inversion lies on the circle beam: no wrap there
    assert w.events[0].beam.kappa == 0 and w.events[1].beam.kappa != 0
    assert w.events[1].kind == "hit", "the circle beam's next event is its re-hit of the circle (pop)"
    # the pop restores the straight beam, which wraps again: every infinity
    # event lies on a straight beam
    assert any(e.kind == "infinity" for e in w.events)
    assert all(e.beam.kappa == 0 for e in w.events if e.kind == "infinity")


def test_infinity_only_plan_aims_away_and_reaches_cursor():
    # plan [inf]: the second realization of the empty plan -- shoot away from
    # the cursor, wrap, come back onto it
    with emit_infinity():
        w = T.planned([], (0, 0), (300, 0), [(T.INFINITY_ID, "")])
    assert kinds(w) == ["infinity", "cursor", "escape"]
    assert w.events[0].applied, "the wrap consumed the infinity entry"
    assert abs(w.events[1].point - mp.mpc(300, 0)) < mp.mpf(10) ** -40
    # the aim is reversed: the beam departs away from the cursor
    assert w.events[0].beam.tau.real < 0
    # without the entry the direct realization reaches the cursor first
    with emit_infinity():
        w0 = T.planned([], (0, 0), (300, 0), [])
    assert kinds(w0) == ["cursor", "infinity", "escape"]
    assert not w0.events[1].applied


def test_infinity_then_mirror_is_the_other_realization():
    # vertical mirror x = 200 between the player (0, 0) and the cursor
    # (300, 50): the cursor's image (100, 50) lies before the mirror. The two
    # realizations of plan [M]: direct -- pass the image, hit M, the reflected
    # beam wraps (an unapplied infinity event) and returns onto the cursor; or
    # [inf, M] -- away, wrap (applied), hit M from the other side, reflect
    # straight onto the cursor.
    m = line(1, (200, -300), (200, 300), REFL, REFL)
    with emit_infinity():
        direct = T.planned([m], (0, 0), (300, 50), [(1, "RIGHT")])
        assert kinds(direct)[:3] == ["hit", "infinity", "cursor"], kinds(direct)
        assert direct.events[0].applied and not direct.events[1].applied
        found = None
        for side in ("LEFT", "RIGHT"):
            w = T.planned([m], (0, 0), (300, 50), [(T.INFINITY_ID, ""), (1, side)])
            if "cursor" in kinds(w):
                found = (side, w)
        assert found is not None
        side, w = found
        ks = kinds(w)
        assert ks[:3] == ["infinity", "hit", "cursor"], ks
        assert w.events[0].applied and w.events[1].applied and w.events[1].surface_id == 1
        # physical twin: same decisions through the cursor
        wp = T.physical([m], (0, 0), (300, 50), [(T.INFINITY_ID, ""), (1, side)])
        assert kinds(wp)[:3] == ["infinity", "hit", "cursor"]


def test_mirror_then_infinity_wraps_after_the_reflection():
    # mirror x = 200 ahead of the player; cursor (600, 50) beyond it: the
    # reflected beam heads back, wraps, and returns onto the cursor
    m = line(1, (200, -300), (200, 300), REFL, REFL)
    with emit_infinity():
        found = None
        for side in ("LEFT", "RIGHT"):
            w = T.planned([m], (0, 0), (600, 50), [(1, side), (T.INFINITY_ID, "")])
            if "cursor" in kinds(w):
                found = w
        assert found is not None
        ks = kinds(found)
        assert ks[:3] == ["hit", "infinity", "cursor"], ks
        assert found.events[1].applied
        # the misplaced entry order does not reach the cursor: [inf, M]
        other = [kinds(T.planned([m], (0, 0), (600, 50), [(T.INFINITY_ID, ""), (1, s)])) for s in ("LEFT", "RIGHT")]
        assert all("cursor" not in k for k in other), other


INV_CIRCLE = {"id": 1, "line": False, "start": [700, 0], "end": [400, -150], "via": [400, 150],
              "full": True, "circle": {"center": [500, 0], "radius": 200},
              "left": {"type": "inversion", "interactive": True}, "right": {"type": "inversion", "interactive": True}}


def test_infinity_after_inversion_is_the_center_passage():
    # Stage 101: after an inversion the frame's infinity is the center; the
    # circle beam passes it (an infinity event with that point). With the
    # cursor outside the circle the center comes after the cursor: the entry
    # stays unmet (the cursor is not reached on the plan); with the cursor
    # inside, the reversed epoch passes the center first: the long way round
    c = T.surface_from_json(INV_CIRCLE)
    with emit_infinity():
        for cursor in ((900, 60), (600, 30)):
            reached = []
            for side in ("LEFT", "RIGHT"):
                w = T.planned([c], (0, 0), cursor, [(1, side), (T.INFINITY_ID, "")])
                ks = kinds(w)
                # every finite infinity passage of the inverted frame is the center
                inf = [e for e in w.events if e.kind == "infinity" and e.point is not None]
                assert inf and all(abs(e.point - mp.mpc(500, 0)) < mp.mpf(10) ** -40 for e in inf)
                if "cursor" in ks:
                    reached.append(side)
                    i_inf = next(i for i, e in enumerate(w.events) if e.kind == "infinity" and e.applied)
                    assert i_inf < ks.index("cursor"), "the entry is consumed before the cursor"
            assert reached, "one side reaches the cursor through the center"


# ---------------------------------------------------------------- Stage 101: the pre-transform

PRE = {"id": 9, "line": False, "start": [700, 0], "end": [400, -150], "via": [400, 150],
       "full": True, "circle": {"center": [500, 0], "radius": 200},
       "left": {"type": "inversion", "interactive": False}, "right": {"type": "inversion", "interactive": False},
       "pre_transform": True}


def test_pre_transform_moves_infinity_to_the_center():
    sc = T.scene_from_json([PRE])
    assert T.hittable(sc) == []
    assert T.pre_q(sc) == mp.mpc(500, 0)
    # a direction through Q0: a straight beam passing the center at a finite t
    w = T.trace_physical_direction(sc, (0, 0), mp.mpc(1, 0))
    assert kinds(w) == ["infinity", "escape"]
    assert abs(w.events[0].point - mp.mpc(500, 0)) < mp.mpf(10) ** -40
    assert abs(w.events[0].t - 500) < mp.mpf(10) ** -40
    # any other direction: a circle through Q0 that passes it and returns
    w = T.trace_physical_direction(sc, (0, 0), mp.mpc(1, 0.5))
    assert kinds(w) == ["infinity", "origin"]
    assert w.events[0].beam.kappa != 0


def test_pre_transform_two_realizations():
    sc = T.scene_from_json([PRE])
    direct = T.planned(sc, (0, 0), (300, 100), [])
    assert kinds(direct) == ["cursor", "infinity", "origin"]
    assert not direct.events[1].applied
    long_way = T.planned(sc, (0, 0), (300, 100), [(T.INFINITY_ID, "")])
    assert kinds(long_way) == ["infinity", "cursor", "origin"]
    assert long_way.events[0].applied
    # the same circle, run the other way
    assert abs(direct.events[0].beam.kappa + long_way.events[0].beam.kappa) < mp.mpf(10) ** -40
    phys = T.physical(sc, (0, 0), (300, 100), [(T.INFINITY_ID, "")])
    assert kinds(phys) == ["infinity", "cursor", "origin"]


def test_pre_transform_mirror_moves_q():
    m = line(1, (400, -300), (400, 300), REFL, REFL)
    sc = T.scene_from_json([PRE]) + [m]
    w = T.trace_physical_direction(sc, (0, 0), mp.mpc(1, 0.3))
    ks = kinds(w)
    assert ks[0] == "hit" and w.events[0].applied
    i = ks.index("infinity")
    # Q after the reflection: the mirror image of the center, (300, 0)
    assert abs(w.events[i].point - mp.mpc(300, 0)) < mp.mpf(10) ** -40


def test_pre_transform_cursor_at_q_is_a_straight_shot():
    # the aim point at the frame's infinity: the line through both; the cursor
    # comes first at the tie, the passage after it
    sc = T.scene_from_json([PRE])
    w = T.planned(sc, (0, 0), (500, 0), [])
    assert kinds(w) == ["cursor", "infinity", "escape"]
    assert w.events[0].beam.kappa == 0


def test_pre_transform_player_at_q_is_a_straight_shot():
    sc = T.scene_from_json([PRE])
    w = T.trace_physical_direction(sc, (500, 0), mp.mpc(1, 0))
    assert kinds(w) == ["escape"]
    # planned from Q: the straight line to the cursor; infinity first never passes Q (it is the start)
    assert kinds(T.planned(sc, (500, 0), (300, 100), [])) == ["cursor", "escape"]
    assert "cursor" not in kinds(T.planned(sc, (500, 0), (300, 100), [(T.INFINITY_ID, "")]))


def test_projective_exit_away_reaches_cursor_through_infinity():
    # a through line-normal projection on x = 300 with the cursor on the
    # approach side: the exit points away, the cursor is reached through the
    # wrap. The infinity entry in the projective's epoch is optional (the exit
    # fixes the orientation): with it the wrap is applied, without it it passes.
    proj = {"type": "line_normal_projection", "interactive": True, "back": False}
    ln = line(1, (300, -400), (300, 400), proj, proj)
    with emit_infinity():
        for plan in ([(1, "RIGHT")], [(1, "RIGHT"), (T.INFINITY_ID, "")]):
            w = T.planned([ln], (100, -100), (100, 200), plan)
            ks = kinds(w)
            assert ks[:3] == ["hit", "infinity", "cursor"], (plan, ks)
            assert w.events[1].applied == (len(plan) == 2)
