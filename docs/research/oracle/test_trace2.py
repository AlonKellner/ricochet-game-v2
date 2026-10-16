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


# ---------------------------------------------------------------- Stage 105: tangency at the frame's infinity point

import contextlib as _ctx
import random as _random
from fractions import Fraction as _Fr

REFL2 = {"type": "reflection", "interactive": True}
PASS2 = {"type": "pass", "interactive": False}
INV2 = {"type": "inversion", "interactive": True}


def circ(sid, c, r, left, right, pre=False):
    d = {"id": sid, "line": False, "start": [c[0] + r, c[1]], "end": [c[0] - r / 2.0, c[1] - 0.866 * r],
         "via": [c[0] - r / 2.0, c[1] + 0.866 * r], "full": True, "circle": {"center": list(c), "radius": r},
         "left": left, "right": right}
    if pre:
        d["pre_transform"] = True
    return d


def lin(sid, s, e, left, right):
    return {"id": sid, "line": True, "start": list(s), "end": list(e),
            "via": [(s[0] + e[0]) / 2.0, (s[1] + e[1]) / 2.0], "full": False, "left": left, "right": right}


PRE0 = circ(9, (500.0, 0.0), 200.0, {"type": "inversion", "interactive": False},
            {"type": "inversion", "interactive": False}, pre=True)


@_ctx.contextmanager
def spy_tangents():
    """The (surface id, Q) pairs the rule skipped."""
    seen = []
    orig = T._pole_tangent

    def spy(pole, seg):
        r = orig(pole, seg)
        if r:
            seen.append((seg.id, (pole[0], pole[1])))
        return r
    T._pole_tangent = spy
    try:
        yield seen
    finally:
        T._pole_tangent = orig


def hits_on(walk, sid):
    return [e for e in walk.events if e.kind == "hit" and e.surface_id == sid]


def test_pole_transitions_are_exact():
    # reflection in x = 650 moves Q (500, 0) to (800, 0) and keeps a horizontal direction
    m = T.surface_from_json(lin(1, (650, -500), (650, 500), REFL2, REFL2))
    p = T._pole_reflect((_Fr(500), _Fr(0), _Fr(1), _Fr(0)), m.seg)
    assert p[:2] == (_Fr(800), _Fr(0)) and p[3] == 0
    # inversion in the circle (0, 0) r 10 of Q (20, 0): Q' (5, 0); a vertical direction stays vertical
    c = T.surface_from_json(circ(2, (0.0, 0.0), 10.0, INV2, INV2))
    p = T._pole_invert((_Fr(20), _Fr(0), _Fr(0), _Fr(1)), c.seg)
    assert p[:2] == (_Fr(5), _Fr(0)) and p[2] == 0 and p[3] != 0
    # Q at the center goes to infinity
    assert T._pole_invert((_Fr(0), _Fr(0), _Fr(1), _Fr(0)), c.seg) is None
    # a straight beam (line y = 3: 0 x + 1 y = 3) inverted: Q = the center, direction horizontal
    p = T._pole_from_line((_Fr(0), _Fr(1), _Fr(3)), c.seg)
    assert p[:2] == (_Fr(0), _Fr(0)) and p[3] == 0 and p[2] != 0
    # a zero direction is no record
    assert T._pole_ok((_Fr(0), _Fr(0)), (_Fr(0), _Fr(0))) is None


def test_pre_transform_tangent_mirror_is_a_touch():
    # Q0 = (500, 0); the circle through P (400, 200) tangent to (200, -150) there and
    # through Q0 is centered (250, 0): tangent to the vertical x = 500 at Q0
    m = lin(1, (500, -300), (500, 300), REFL2, REFL2)
    sc = T.scene_from_json([PRE0, m])
    with spy_tangents() as seen:
        w = T.trace_physical_direction(sc, (400, 200), mp.mpc(200, -150))
    assert (1, (_Fr(500), _Fr(0))) in seen
    assert hits_on(w, 1) == [], kinds(w)
    assert kinds(w)[0] == "infinity"
    # control: a transversal aim crosses the mirror at Q0 (hits before the passage at a tie)
    w = T.trace_physical_direction(sc, (400, 200), mp.mpc(1, 0))
    assert hits_on(w, 1), kinds(w)


def test_pre_transform_tangent_circle_is_a_touch():
    # a pass circle centered (600, 0) r 100 is tangent at Q0 to the beam circle centered (250, 0)
    c = circ(2, (600.0, 0.0), 100.0, PASS2, PASS2)
    sc = T.scene_from_json([PRE0, c])
    with spy_tangents() as seen:
        w = T.trace_physical_direction(sc, (400, 200), mp.mpc(200, -150))
    assert any(s[0] == 2 for s in seen)
    assert hits_on(w, 2) == [], kinds(w)


def test_one_ulp_off_is_not_a_tangency():
    m = lin(1, (500, -300), (500, 300), REFL2, REFL2)
    sc = T.scene_from_json([PRE0, m])
    import math
    with spy_tangents() as seen:
        w = T.trace_physical_direction(sc, (400, 200), mp.mpc(200, math.nextafter(-150.0, 0.0)))
    assert seen == []
    assert hits_on(w, 1), kinds(w)


def test_identity_frame_inversion_center_on_a_mirror():
    # the straight aim y = 100, parallel to the mirror y = 0 through the inversion
    # center (500, 0): its image is a circle tangent to the mirror at the center
    inv = circ(2, (500.0, 0.0), 200.0, INV2, INV2)
    m = lin(1, (0, 0), (1000, 0), REFL2, REFL2)
    sc = T.scene_from_json([inv, m])
    with spy_tangents() as seen:
        w = T.trace_physical_direction(sc, (0, 100), mp.mpc(1, 0))
    assert (1, (_Fr(500), _Fr(0))) in seen
    assert all(abs(e.point - mp.mpc(500, 0)) > 1 for e in hits_on(w, 1))


def test_reflected_pole_meets_a_second_mirror():
    # as above, then the image circle (center (500, 200), r 200) reflects in x = 650:
    # Q' = (800, 0), the direction stays horizontal: tangent to the mirror y = 0 on 700..900
    inv = circ(2, (500.0, 0.0), 200.0, INV2, INV2)
    m1 = lin(1, (650, -500), (650, 500), REFL2, REFL2)
    m3 = lin(3, (700, 0), (900, 0), REFL2, REFL2)
    sc = T.scene_from_json([inv, m1, m3])
    with spy_tangents() as seen:
        w = T.trace_physical_direction(sc, (0, 100), mp.mpc(1, 0))
    assert (3, (_Fr(800), _Fr(0))) in seen, seen
    assert all(abs(e.point - mp.mpc(800, 0)) > 1 for e in hits_on(w, 3))


def test_translation_portal_moves_the_pole_k1():
    # K1: the portals playground, the vertical aim at x = 1243.333
    import json as _json, struct as _struct
    d = _json.load(open(os.path.join(os.path.dirname(__file__), "..", "..", "..", "bench", "scenes", "playground__portals.surfaces.json")))
    sc = T.scene_from_json(d["surfaces"])
    f32 = lambda x: _struct.unpack("f", _struct.pack("f", x))[0]
    p, c = (f32(1243.333), 690.0), (f32(1243.333), 390.0)
    with spy_tangents() as seen:
        a = T.physical(sc, p, c, [])
        b = T.physical(sc, p, c, [(8, "LEFT"), (8, "LEFT")])
    assert any(s[0] == 5 for s in seen)
    assert hits_on(a, 5) == [] and hits_on(b, 5) == []
    assert kinds(a) == kinds(b)
    # without the rule the oracle's own answer depends on its precision
    with _ctx.ExitStack():
        orig = T._pole_tangent
        T._pole_tangent = lambda pole, seg: False
        try:
            answers = set()
            with mp.workdps(80):
                sc_d = T.scene_from_json(d["surfaces"])
            for dps in (80, 200):
                with mp.workdps(dps):
                    answers.add(len(hits_on(T.physical(sc_d, p, c, []), 5)))
        finally:
            T._pole_tangent = orig
    assert len(answers) == 2, "rounding decided the tangency without the rule"


def test_rotated_portal_drops_the_record():
    import math
    src = T.surface_from_json({"id": 1, "line": True, "start": [100, -500], "end": [100, 500], "via": [100, 0],
                               "full": False, "left": {"type": "portal", "interactive": True, "theta": math.pi / 2, "d": [0, 0], "pair": 2},
                               "right": {"type": "portal", "interactive": True, "theta": math.pi / 2, "d": [0, 0], "pair": 2}})
    assert T._pole_apply((_Fr(1), _Fr(2), _Fr(1), _Fr(0)), mp.mpc(1, 2), None, src) is None


def test_pop_and_projective_clear_or_restore_the_record():
    # a projective clears the record (its exit is straight in screen space)
    proj = {"type": "line_normal_projection", "interactive": True, "back": False}
    ln = lin(1, (450, -400), (450, 400), proj, proj)
    sc = T.scene_from_json([PRE0, ln])
    records = []
    T.POLE_HOOK = lambda walk, beam: records.append(walk.pole)
    try:
        T.trace_physical_direction(sc, (300, 50), mp.mpc(1, 0.2))
    finally:
        T.POLE_HOOK = None
    assert records[0] is not None and records[-1] is None
    # a pop restores it: the inversion and its re-hit (identity frame: no record before, none after)
    inv = circ(2, (500.0, 0.0), 200.0, INV2, INV2)
    records = []
    T.POLE_HOOK = lambda walk, beam: records.append(walk.pole)
    try:
        T.trace_physical_direction(T.scene_from_json([inv]), (0, 100), mp.mpc(1, 0))
    finally:
        T.POLE_HOOK = None
    assert records[0] is None and any(r is not None for r in records) and records[-1] is None


def test_planned_infinity_entry_consumed_at_the_tangent_point():
    m = lin(1, (500, -300), (500, 300), REFL2, REFL2)
    sc = T.scene_from_json([PRE0, m])
    # P (400, 200) and X (100, 200) on the circle centered (250, 0) through Q0
    for plan in ([], [(T.INFINITY_ID, "")]):
        for mode in (T.planned, T.physical):
            w = mode(sc, (400, 200), (100, 200), plan)
            assert hits_on(w, 1) == [], kinds(w)
            assert "cursor" in kinds(w)
    w = T.planned(sc, (400, 200), (100, 200), [(T.INFINITY_ID, "")])
    ks = kinds(w)
    assert ks.index("infinity") < ks.index("cursor") and w.events[ks.index("infinity")].applied


def test_pole_direction_is_the_beam_tangent_property():
    # random exact scenes: wherever the record exists, Q is the walk's Q and D is
    # parallel to the beam's tangent there
    rng = _random.Random(105)
    checked = 0
    for k in range(60):
        surfs = [circ(20, (rng.randint(3, 15) * 64.0, rng.randint(3, 10) * 64.0), rng.randint(2, 4) * 32.0,
                      {"type": "inversion", "interactive": False}, {"type": "inversion", "interactive": False}, pre=True)] if k % 2 else []
        for i in range(rng.randint(2, 5)):
            if rng.random() < 0.5:
                surfs.append(circ(i + 1, (rng.randint(2, 28) * 64.0, rng.randint(2, 15) * 64.0), rng.randint(1, 5) * 32.0, INV2, INV2))
            else:
                a = (rng.randint(1, 29) * 64.0, rng.randint(1, 16) * 64.0)
                b = (a[0] + rng.choice([0, 128, -128, 256]), a[1] + rng.choice([0, 128, 256, -128]))
                if a == b:
                    b = (a[0] + 64.0, a[1])
                surfs.append(lin(i + 1, a, b, REFL2, REFL2))
        sc = T.scene_from_json(surfs)

        def hook(walk, beam):
            nonlocal checked
            if walk.pole is None or walk.Q is None:
                return
            q = mp.mpc(float(walk.pole[0]), float(walk.pole[1]))
            assert abs(q - walk.Q) < mp.mpf(10) ** -10 * (1 + abs(q))
            t = beam.param_of(walk.Q)
            tg = beam.tangent_at(t)
            dd = mp.mpc(float(walk.pole[2]), float(walk.pole[3]))
            cr = abs((tg.real * dd.imag - tg.imag * dd.real) / abs(dd))
            assert cr < mp.mpf(10) ** -12, (k, cr)
            checked += 1
        T.POLE_HOOK = hook
        try:
            T.trace_physical_direction(sc, (rng.randint(1, 29) * 64.0 + 7, rng.randint(1, 16) * 64.0 + 3),
                                       mp.mpc(rng.choice([1, -1, 3]), rng.choice([0, 1, -2])))
        finally:
            T.POLE_HOOK = None
    assert checked > 30


def _structure(sc, p, d, dps):
    with mp.workdps(dps):
        w = T.trace_physical_direction(sc, p, mp.mpc(*d))
    return [(e.kind, e.surface_id, e.applied) for e in w.events]


def test_departing_from_q_is_precision_independent():
    # a mirror through Q0 = (576, 320) reflects at Q, the beam returns there (the
    # departure re-hit, again from Q); a pass circle through Q is crossed at the
    # departure point at the end of each loop, the mirror's own root there is the departure
    pre = circ(1, (576.0, 320.0), 64.0, {"type": "inversion", "interactive": False},
               {"type": "inversion", "interactive": False}, pre=True)
    sc = T.scene_from_json([pre, circ(2, (608.0, 320.0), 32.0, PASS2, PASS2),
                            lin(3, (576, 256), (576, 384), REFL2, REFL2)])
    a = _structure(sc, (608, 528), (0, -1), 80)
    assert a == _structure(sc, (608, 528), (0, -1), 200)
    assert sum(1 for k in a if k == ("hit", 3, True)) >= 3, a
    # a straight beam through Q0 (player, Q0 and the direction collinear): the
    # concurrent mirror and pass line through Q tie exactly there, the mirror first
    pre = circ(1, (448.0, 704.0), 96.0, {"type": "inversion", "interactive": False},
               {"type": "inversion", "interactive": False}, pre=True)
    sc = T.scene_from_json([pre, lin(2, (448, 505), (448, 903), REFL2, REFL2),
                            lin(3, (313, 704), (583, 704), PASS2, PASS2),
                            lin(4, (720, 136), (816, 176), REFL2, REFL2)])
    a = _structure(sc, (672, 592), (2, -1), 80)
    assert a == _structure(sc, (672, 592), (2, -1), 200)
    assert a[1] == ("hit", 2, True), a


# ---------------------------------------------------------------- Stage 106: the departure point after a pop

import json as _json

_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TERM2 = {"type": "terminal", "interactive": False}
BOX2 = [lin(90, (0, 0), (1920, 0), TERM2, TERM2), lin(91, (1920, 0), (1920, 1080), TERM2, TERM2),
        lin(92, (1920, 1080), (0, 1080), TERM2, TERM2), lin(93, (0, 1080), (0, 0), TERM2, TERM2)]


def hit_list(walk):
    return [(e.surface_id, e.applied, e.point) for e in walk.events if e.kind == "hit"]


def hit_ids(walk):
    return [(e.surface_id, e.applied) for e in walk.events if e.kind == "hit"]


def no_reapply_in_place(walk):
    """No zero-length loop: an applied hit, then only cursor / infinity events at the
    same point, then an applied hit on the same surface at the same point."""
    last = None
    for e in walk.events:
        if e.kind in ("cursor", "infinity") and last is not None and e.point == last.point:
            continue
        if e.kind == "hit" and e.applied and last is not None and e.surface_id == last.surface_id and e.point == last.point:
            return False
        last = e if e.kind == "hit" and e.applied else None
    return True


@_ctx.contextmanager
def known_lists():
    """Every loop's known list (the record beside each beam)."""
    seen = []
    orig = T.POLE_HOOK

    def hook(walk, beam):
        seen.append(list(walk.known))
        if orig is not None:
            orig(walk, beam)
    T.POLE_HOOK = hook
    try:
        yield seen
    finally:
        T.POLE_HOOK = orig


def _at(dps, f):
    with mp.workdps(dps):
        return f()


def test_k2_pop_at_the_players_point_keeps_the_other_crossing():
    # tangent_infinity.tscn: the player on the inversion circle's top (1160, 440),
    # the aim image under the mirror plan at its bottom (1160, 640): the beam
    # inverts in at the image and out at the player (a pop at a known point)
    dump = _json.load(open(os.path.join(_ROOT, "bench", "scenes", "playground__tangent_infinity.surfaces.json")))
    sc = T.scene_from_json(dump["surfaces"])
    with known_lists() as seen:
        w = T.physical(sc, (1160, 440), (760, 640), [(6, "LEFT")])
    ref = T.physical(sc, (1160, 440), (1160, 640), [])
    assert hit_ids(w) == hit_ids(ref), (kinds(w), kinds(ref))
    hl = hit_list(w)
    assert hl[0][:2] == (8, True) and hl[0][2] == P((1160, 640))
    assert hl[1][:2] == (8, True) and hl[1][2] == P((1160, 440)), hl[:3]
    assert hl[2][:2] == (8, True) and hl[2][2] == P((1160, 640)), hl[:3]   # the other crossing again, not the player
    assert no_reapply_in_place(w) and no_reapply_in_place(ref)
    # the pop tagged the player's point as the departure point of the circle
    assert any(any(k[0] == P((1160, 440)) and k[2] == 8 for k in known) for known in seen)
    assert _at(200, lambda: hit_ids(T.physical(sc, (1160, 440), (760, 640), [(6, "LEFT")]))) == hit_ids(w)


def _arc_scene():
    # the right half of circle C (960, 540) r 200 as a reflective arc: S (960, 340),
    # V (1160, 540), E (960, 740); its carrier's left half is not a segment
    arc = {"id": 1, "line": False, "start": [960.0, 340.0], "end": [960.0, 740.0], "via": [1160.0, 540.0],
           "full": False, "left": REFL2, "right": REFL2}
    return T.scene_from_json([arc] + BOX2)


def test_pop_at_the_cursor_on_an_arc_continues_past_it():
    sc = _arc_scene()
    # the aim from (600, 300) through the cursor (1160, 540) on the arc: enters the
    # carrier off-segment (a pass), reaches the cursor, reflects there (push), the
    # image circle passes the carrier's other crossing (pass) and returns to the
    # cursor (the departure re-hit: a pop), then the line goes on to the box
    w = T.planned(sc, (600, 300), (1160, 540), [])
    ids = hit_ids(w)
    assert ids[:5] == [(1, False), (1, True), (1, False), (1, True), (91, True)], (ids, kinds(w))
    assert kinds(w).index("cursor") == 1
    assert no_reapply_in_place(w)
    ph = T.physical(sc, (600, 300), (1160, 540), [])
    assert no_reapply_in_place(ph)
    for dps in (20, 30, 200):
        assert _at(dps, lambda: hit_ids(T.planned(sc, (600, 300), (1160, 540), []))) == ids, dps


def test_periodic_orbit_through_a_player_on_an_inversion_circle():
    c = circ(1, (960.0, 540.0), 200.0, INV2, INV2)
    sc = T.scene_from_json([c] + BOX2)
    # the player on the circle at (760, 540), aiming through the center: the line
    # maps to itself, the beam inverts at (1160, 540), comes back to the player's
    # point, inverts again (a pop at a known point), and so on: a closed orbit
    w = T.trace_physical_direction(sc, (760, 540), mp.mpc(1, 0))
    hl = hit_list(w)
    # (the inverted beam passes the center, the frame's image of infinity, on the
    # way back: an infinity event per orbit, counted toward the budget)
    assert len(hl) >= 20 and kinds(w).count("infinity") >= 10 and kinds(w)[-1] == "max", kinds(w)
    for i, (sid, applied, pt) in enumerate(hl):
        assert sid == 1 and applied
        assert pt == P((1160, 540)) if i % 2 == 0 else pt == P((760, 540)), (i, pt)
    aimed = T.physical(sc, (760, 540), (1160, 540), [])
    assert hit_ids(aimed) == hit_ids(w)
    assert _at(200, lambda: hit_list(T.trace_physical_direction(sc, (760, 540), mp.mpc(1, 0)))) == hl


def test_a_known_point_at_the_other_crossing_still_snaps_it():
    c = circ(1, (960.0, 540.0), 200.0, INV2, INV2)
    sc = T.scene_from_json([c] + BOX2)
    # the cursor on the circle at (1160, 540), the player inside at (860, 540):
    # the planned path reaches the cursor exactly (the known point pins the root)
    w = T.planned(sc, (860, 540), (1160, 540), [])
    ev = w.events
    assert ev[0].kind == "cursor" and ev[0].point == P((1160, 540)), kinds(w)
    assert ev[1].kind == "hit" and ev[1].surface_id == 1 and ev[1].applied and ev[1].point == P((1160, 540))
    assert no_reapply_in_place(w)
    assert _at(200, lambda: hit_ids(T.planned(sc, (860, 540), (1160, 540), []))) == hit_ids(w)


def test_portal_pop_by_the_departure_rehit_is_unchanged():
    portal = {"type": "portal", "interactive": True, "d": [800.0, 0.0], "theta": 0.0, "pair": 2}
    a = lin(1, (400, 200), (400, 700), portal, portal)
    b = lin(2, (1200, 200), (1200, 700), {"type": "portal", "interactive": True, "d": [-800.0, 0.0], "theta": 0.0, "pair": 1},
            {"type": "portal", "interactive": True, "d": [-800.0, 0.0], "theta": 0.0, "pair": 1})
    m = lin(3, (1500, 100), (1500, 900), REFL2, REFL2)
    sc = T.scene_from_json([a, b, m] + BOX2)
    w = T.trace_physical_direction(sc, (300, 450), mp.mpc(1, 0))
    ids = hit_ids(w)
    assert ids[:3] == [(1, True), (3, True), (2, True)], (ids, kinds(w))
    pops = [e for e in w.events if e.kind == "hit" and e.surface_id == 2 and e.applied]
    assert pops and pops[0].point == P((1200, 450))
    assert no_reapply_in_place(w)
    assert _at(200, lambda: hit_ids(T.trace_physical_direction(sc, (300, 450), mp.mpc(1, 0)))) == ids


def test_k3_degenerate_aim_shoots_through_the_cursor():
    # tangent_infinity.tscn: the cursor (1085, 665) is the player's (835, 665) mirror
    # image in the planned mirror x = 960: the aim image is the player, every
    # direction solves the plan. The convention (GAME_SPEC 11.4): straight at the
    # cursor, i.e. in this pre-transform frame the circle through the player, the
    # cursor and Q0 = (960, 540): center (960, 665), radius 125, run player -> cursor
    dump = _json.load(open(os.path.join(_ROOT, "bench", "scenes", "playground__tangent_infinity.surfaces.json")))
    sc = T.scene_from_json(dump["surfaces"])
    w = T.physical(sc, (835, 665), (1085, 665), [(6, "LEFT")])
    b0 = w.events[0].beam
    assert b0.p == P((835, 665)) and abs(abs(b0.kappa) - mp.mpf(1) / 125) < mp.mpf(10) ** -60, b0
    assert abs(b0.at(b0.param_of(P((1085, 665)))) - P((1085, 665))) < mp.mpf(10) ** -60
    # the first crossing is the mirror's off-segment root (960, 790), not Q0
    assert w.events[0].kind == "hit" and w.events[0].surface_id == 6 and w.events[0].point == P((960, 790)), kinds(w)
    assert not w.events[0].applied
    assert no_reapply_in_place(w)
    pl = T.planned(sc, (835, 665), (1085, 665), [(6, "LEFT")])
    assert "cursor" in kinds(pl) and no_reapply_in_place(pl), kinds(pl)
    assert _at(200, lambda: hit_ids(T.physical(sc, (835, 665), (1085, 665), [(6, "LEFT")]))) == hit_ids(w)


# ---------------------------------------------------------------- Stage 107: state lists, presence

def test_side_states_reads_the_list_and_the_legacy_single_change():
    a = {"key": "a", "op": "set", "value": 1}
    b = {"key": "b", "op": "toggle", "value": True}
    assert T.side_states(None) == []
    assert T.side_states({"type": "pass"}) == []
    assert T.side_states({"type": "pass", "state": a}) == [a]
    assert T.side_states({"type": "pass", "states": [a, b]}) == [a, b]
    assert T.apply_states({}, [a, b]) == {"a": 1, "b": True}


def test_ghost_toggler_is_any_write_of_ghost():
    g = {"key": "ghost", "op": "toggle", "value": True}
    assert T.is_ghost_toggler({"type": "pass", "states": [{"key": "x", "op": "set", "value": 1}, g]})
    assert not T.is_ghost_toggler({"type": "pass", "states": [{"key": "x", "op": "set", "value": 1}]})


def test_presence_conditions_are_anded_and_default_is_a_missing_key():
    pres = [{"key": "c", "values": ["DEFAULT", "armed"]}, {"key": "d", "values": ["true"]}]
    assert not T.present(pres, {})
    assert T.present(pres, {"d": True})
    assert T.present(pres, {"c": "armed", "d": True})
    assert not T.present(pres, {"c": "pending", "d": True})
    assert T.present(None, {}) and T.present([], {"c": 1})


def test_absent_surface_is_no_crossing_and_counts_no_hit():
    mirror = T.surface_from_json({"id": 1, "line": True, "start": [100, -500], "end": [100, 500], "via": [100, 0],
                                  "full": False, "left": REFL, "right": REFL,
                                  "presence": [{"key": "m", "values": ["true"]}]})
    wall = line(2, (300, -500), (300, 500), TERM, TERM)
    w = T.trace_physical_direction([mirror, wall], (0, 0), mp.mpc(1, 0))
    assert hits(w) == [(2, True, True)]
    w = T.trace_physical_direction([mirror, wall], (0, 0), mp.mpc(1, 0), flags={"m": True})
    assert hits(w)[0] == (1, True, False)


def test_plan_effects_skip_an_absent_entry_and_its_writes():
    s = T.surface_from_json({"id": 1, "line": True, "start": [100, -500], "end": [100, 500], "via": [100, 0],
                             "full": False, "left": dict(REFL, states=[{"key": "k", "op": "set", "value": 2}]),
                             "right": REFL, "presence": [{"key": "m", "values": ["true"]}]})
    assert T.plan_resolved([s], [(1, T.LEFT)], {}) == [(None, {})]
    assert T.plan_resolved([s], [(1, T.LEFT), (1, T.LEFT)], {"m": True}) == [
        ("reflection", {"m": True}), ("reflection", {"m": True, "k": 2})]


# ---------------------------------------------------------------- K6 (Stage 109): one carrier, shared crossings

def test_surfaces_on_one_carrier_share_their_crossings():
    # three segments on the line x = 1100 (different spans, so different
    # defining points); a beam crossing it meets them at one parameter
    a = line(5, (1100, 350), (1100, 450), PASS, PASS)
    b = line(3, (1100, 620), (1100, 760), PASS, PASS)
    c = line(9, (1100, -50), (1100, 100), PASS, PASS)
    assert T.carrier_groups([a, b, c]) == {5: 5, 3: 5, 9: 5}
    w = T.trace_physical_direction([a, b, c], (310.37, 211.9), mp.mpc(1, 0.2117))
    hs = [e for e in w.events if e.kind == "hit"][:3]
    assert len({e.t for e in hs}) == 1, [e.t for e in hs]
    assert [e.surface_id for e in hs] == [3, 5, 9], "a tie: by id"


def test_one_circle_carrier_groups_arcs():
    full = T.surface_from_json({"id": 1, "line": False, "start": [600, 0], "end": [450, -86.6], "via": [450, 86.6],
                                "full": True, "circle": {"center": [500, 0], "radius": 100}, "left": REFL, "right": REFL})
    arc = T.surface_from_json({"id": 2, "line": False, "start": [600, 0], "end": [400, 0], "via": [500, 100],
                               "full": False, "left": REFL, "right": REFL})
    other = T.surface_from_json({"id": 3, "line": False, "start": [610, 0], "end": [410, 0], "via": [510, 100],
                                 "full": False, "left": REFL, "right": REFL})
    assert T.carrier_groups([full, arc, other]) == {1: 1, 2: 1, 3: 3}
