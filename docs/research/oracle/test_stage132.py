"""Stage 132 (GAME_SPEC 4.2, 12.6, 13.1): the ray's origin is a plan entry,
ORIGIN_ID, consumed by its passage in both walks; the realization counts the
infinity and origin entries (the half of the ray's line the walk is on)."""
import os
import struct
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
import visibility as V  # noqa: E402
from test_trace2 import PASS, emit_infinity, kinds, line  # noqa: E402

mp.mp.dps = 50
INF = (T.INFINITY_ID, "")
ORG = (T.ORIGIN_ID, "")


def _h(s):
    return struct.unpack("f", struct.pack("f", float.fromhex(s)))[0]


def test_the_entries_decide_the_half():
    assert not T.behind_origin([])
    assert T.behind_origin([INF]) and not T.behind_origin([INF, ORG])
    assert T.behind_origin([(5, "LEFT"), INF]) and not T.behind_origin([(5, "LEFT")])


def test_infinity_then_origin_aims_at_the_cursor_and_reaches_it_on_the_next_lap():
    # an open scene: [inf, O] -- the aim at the cursor (direct), passed with the
    # entries pending, the wrap, the player (the origin), the cursor
    p = line(1, (500, -500), (500, 500), PASS, PASS)
    with emit_infinity():
        pl = T.planned([p], (0.0, 0.0), (300.0, 0.0), [INF, ORG])
        ph = T.physical([p], (0.0, 0.0), (300.0, 0.0), [INF, ORG])
        assert pl.events[0].beam.tau.real > 0, "aimed at the cursor"
        ks = [e.kind for e in pl.events if e.kind != "hit"]
        assert ks[:3] == ["infinity", "origin", "cursor"], kinds(pl)
        o = next(e for e in ph.events if e.kind == "origin")
        assert o.applied and not o.off_plan and "cursor" in kinds(ph)
        assert V.visible([p], (0.0, 0.0), (300.0, 0.0), [INF, ORG]) is True


def test_the_users_dump_with_the_origin_planned_is_visible():
    from level_json import build_defs, load_level  # noqa: E402
    sc = T.scene_from_json(build_defs(load_level("open_room_playable")))
    P = (_h("0x1.7df8aep+10"), _h("0x1.cef2e4p+9"))
    C = (_h("0x1.d7fddcp+9"), _h("0x1.b24de8p+9"))
    hints = {0: (_h("0x1.2d906ap+10"), _h("0x1.36933ep+9")), 1: (_h("0x1.dc7f1p+10"), _h("0x1.90bdfep+4"))}
    with emit_infinity():
        assert V.visible(sc, P, C, [(5, "LEFT"), INF], None, hints) is False
        assert V.visible(sc, P, C, [(5, "LEFT"), INF, ORG], None, hints) is True
        # without the wrap the origin is not reached first: the wrap is unplanned
        assert V.visible(sc, P, C, [(5, "LEFT"), ORG], None, hints) is False


def test_an_inner_circle_normal_projection_plans_its_centre():
    from gen_fixtures_stage85 import circle
    from gen_fixtures_stage87 import proj
    c = circle(1, (500.0, 300.0), 100.0, *(proj("circle_normal_projection", False),) * 2)
    sc = T.scene_from_json([c])
    with emit_infinity():
        assert V.visible(sc, (200.0, 320.0), (550.0, 300.0), [(1, "LEFT")]) is False
        assert V.visible(sc, (200.0, 320.0), (550.0, 300.0), [(1, "LEFT"), ORG]) is True
        ph = T.physical(sc, (200.0, 320.0), (550.0, 300.0), [(1, "LEFT"), ORG])
        o = next(e for e in ph.events if e.kind == "origin")
        assert o.applied and abs(o.point - mp.mpc(500, 300)) < mp.mpf(10) ** -30
