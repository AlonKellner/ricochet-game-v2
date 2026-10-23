"""Stage 131 (GAME_SPEC 12.6, the user, 2026-10-22): no loop detection. A trace
ends at a block, at an escape (a straight beam with the infinity switch off),
or at one of two limits: 10 significant events (applied effects, firing state
changes, passages of infinity), 64 events in all (every carrier crossing). The
cursor counts toward neither."""
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from test_trace2 import PASS, PRE, emit_infinity, kinds, line  # noqa: E402

mp.mp.dps = 50


def test_a_firing_state_change_is_significant_a_plain_pass_is_not():
    plain = line(1, (50, -500), (50, 500), PASS, PASS)
    w = T.trace_physical_direction([plain], (0, 0), mp.mpc(1, 0))
    # (a loop: the wrap, the ray's origin -- both significant; Stage 134: a plain pass
    # line is no candidate, never computed)
    assert kinds(w) == ["infinity", "origin"] * (T.MAX_SIGNIFICANT // 2) + ["max"]
    writes = dict(PASS, states=[{"key": "x", "op": "set", "value": 1}])
    toggler = line(1, (50, -500), (50, 500), writes, writes)
    w = T.trace_physical_direction([toggler], (0, 0), mp.mpc(1, 0))
    assert kinds(w) == ["hit", "infinity", "origin"] * 3 + ["hit", "max"]   # (the toggler's hits significant too)


def _off(sid, x):
    """Stage 134: a wall crossed off its segment by a ray along y = 0 -- a candidate
    whose crossing fires nothing."""
    return line(sid, (x, 100), (x, 200), dict(PASS, type="terminal"), dict(PASS, type="terminal"))


def test_sixty_four_events_in_all():
    lines = [_off(i + 1, 10 * (i + 1)) for i in range(T.MAX_HITS + 6)]
    with emit_infinity():
        w = T.trace_physical_direction(lines, (0, 0), mp.mpc(1, 0))
    assert kinds(w) == ["hit"] * T.MAX_HITS + ["max"]


def test_the_cursor_counts_toward_neither():
    # walls crossed off their segments, the cursor among them (Stage 134: the planned
    # walk computes them past its cursor only, and wraps to the rest)
    lines = [_off(i + 1, 10 * (i + 1)) for i in range(T.MAX_HITS + 6)]
    with emit_infinity():
        pl = T.planned(lines, (0, 0), (205, 0), [])
        ph = T.physical(lines, (0, 0), (205, 0), [])
    assert "cursor" in kinds(pl)
    # (the cursor is not one of the 64: every other event is -- the planned walk's
    # passages of infinity and of the origin too)
    def counted(w):
        return sum(1 for k in kinds(w) if k not in ("cursor", "max"))
    assert counted(pl) == counted(ph) == T.MAX_HITS


def test_an_unplanned_passage_of_infinity_before_the_cursor_is_a_divergence():
    # Stage 131 (the user): the mirror x = 200, the cursor (300, 50) past it; plan [M]:
    # the reflected beam heads back, wraps (unplanned) and returns onto the cursor --
    # the aim goes that way, but the paths diverge at the passage
    import visibility as V  # noqa: E402
    m = line(1, (200, -300), (200, 300), dict(PASS, type="reflection"), dict(PASS, type="reflection"))
    with emit_infinity():
        pl = T.planned([m], (0, 0), (300, 50), [(1, "RIGHT")])
        ph = T.physical([m], (0, 0), (300, 50), [(1, "RIGHT")])
        assert kinds(pl)[:4] == ["hit", "infinity", "origin", "cursor"]   # (the player's image (400, 0) on the way back)
        inf_pl = next(e for e in pl.events if e.kind == "infinity")
        inf_ph = next(e for e in ph.events if e.kind == "infinity")
        assert not inf_pl.off_plan and inf_ph.off_plan
        assert "cursor" not in kinds(ph)
        assert V.visible([m], (0, 0), (300, 50), [(1, "RIGHT")]) is False


def test_a_passage_after_the_cursor_is_the_continuation():
    # no plan, the cursor straight ahead: the walk wraps after it -- no divergence
    import visibility as V  # noqa: E402
    p = line(1, (500, -500), (500, 500), PASS, PASS)
    with emit_infinity():
        ph = T.physical([p], (0, 0), (300, 0), [])
        assert all(not e.off_plan for e in ph.events)
        assert V.visible([p], (0, 0), (300, 0), []) is True


def test_the_ray_origin_is_the_players_image():
    # the user's report (F12, 2026-10-22): open_room_playable, [5 L, inf]: the
    # reflected beam wraps (planned) and comes back through the player's image in
    # the mirror x = 1200 -- the ray's origin -- before the cursor: unplanned, a
    # divergence (the physical walk leaves the plan there)
    import struct
    import visibility as V  # noqa: E402
    from level_json import build_defs, load_level  # noqa: E402

    def h(s):
        return struct.unpack("f", struct.pack("f", float.fromhex(s)))[0]
    sc = T.scene_from_json(build_defs(load_level("open_room_playable")))
    P = (h("0x1.7df8aep+10"), h("0x1.cef2e4p+9"))
    C = (h("0x1.d7fddcp+9"), h("0x1.b24de8p+9"))
    plan = [(5, "LEFT"), (T.INFINITY_ID, "LEFT")]
    hints = {0: (h("0x1.2d906ap+10"), h("0x1.36933ep+9")), 1: (h("0x1.dc7f1p+10"), h("0x1.90bdfep+4"))}
    with emit_infinity():
        pl = T.planned(sc, P, C, plan, None, hints)
        ph = T.physical(sc, P, C, plan, None, hints)
        ks = kinds(pl)
        i = ks.index("cursor")
        o = pl.events[ks.index("origin")]
        assert ks.index("origin") < i and abs(o.point - mp.mpc(2400 - P[0], P[1])) < 1e-9, ks
        o_ph = next(e for e in ph.events if e.kind == "origin")
        assert o_ph.off_plan and "cursor" not in kinds(ph)
        assert V.visible(sc, P, C, plan, None, hints) is False


def test_a_circle_normal_projection_rays_origin_is_the_centre():
    # the user: a circle normal projection's rays run along radii -- their origin
    # is the circle's centre, so passing it is significant, and unplanned before
    # the cursor a divergence
    import visibility as V  # noqa: E402
    from gen_fixtures_stage85 import circle
    from gen_fixtures_stage87 import proj
    c = circle(1, (500.0, 300.0), 100.0, *(proj("circle_normal_projection", False),) * 2)
    sc = T.scene_from_json([c])
    with emit_infinity():
        w = T.trace_physical_direction(sc, (200.0, 320.0), mp.mpc(1, 0))
        ks = kinds(w)
        assert ks[:2] == ["hit", "origin"] and w.events[0].applied, ks
        assert abs(w.events[1].point - mp.mpc(500, 300)) < mp.mpf(10) ** -30
        # a cursor past the centre is not visible without planning it
        pl = T.planned(sc, (200.0, 320.0), (650.0, 300.0), [(1, "LEFT")])
        kp = kinds(pl)
        assert kp.index("origin") < kp.index("cursor"), kp
        assert V.visible(sc, (200.0, 320.0), (650.0, 300.0), [(1, "LEFT")]) is False
        # one before it is
        assert V.visible(sc, (200.0, 320.0), (450.0, 300.0), [(1, "LEFT")]) is True
