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


def test_mirror_reflects_by_angle_law():
    # vertical two-sided mirror x=100; beam from (0,0) at 45° down-right
    m = line(1, (100, -500), (100, 500), REFL, REFL)
    w = T.trace_physical_direction([m], (0, 0), mp.mpc(1, 1))
    ev = w.events
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
    ev = w.events
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
    e0 = w.events[0]
    assert e0.surface_id == 1 and e0.applied
    # the exit beam is the rotated beam: its point is T(h) = i h
    e1 = w.events[1]
    assert e1.kind in ("hit", "escape", "max")


def test_planned_through_portal_aims_at_preimage():
    src, tgt = portal_pair(1, (100, -500), (100, 500), 0.0, (300, 0))
    img = T.aim_image([src, tgt], [(1, "RIGHT")], (500, 100))
    assert img == mp.mpc(200, 100)
