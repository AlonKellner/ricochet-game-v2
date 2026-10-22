"""Stage 129 (GAME_SPEC 9.7, 14.2): a walk's frame says whether it has left the
plan. The user's report (F12 dump, 2026-10-22): on crossings, the mirror circle
17 planned on its western piece; from the player the aim line meets only its
eastern piece, where the planned walk applies the entry (off its segment) and
the physical walk reflects too -- the same effect on the same beam, but out of
plan. The paths diverge at that hit; before Stage 129 the frames (epoch,
stack, flags) were equal and the cursor beyond was visible."""
import os
import struct
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
import visibility as V  # noqa: E402
from level_json import build_defs, load_level  # noqa: E402

mp.mp.dps = 50


def _hexf(h):
    return struct.unpack("f", struct.pack("f", float.fromhex(h)))[0]


SURFS = T.scene_from_json(build_defs(load_level("test_levels/crossings")))
PLAYER = (_hexf("0x1.593f58p+10"), _hexf("0x1.f0316ap+8"))
CURSOR = (_hexf("0x1.932b82p+10"), _hexf("0x1.62a824p+8"))
PLAN = [(17, "LEFT")]
HINTS = {0: (1300.3544921875, 345.6401062011719)}  # the click (float32), plan entry 0
WEST = [{"start": [1400.0, 390.0], "end": [1400.0, 210.0], "via": [1310.0, 300.0]}]


def _hit17(events):
    return next(e for e in events if e.kind == "hit" and e.surface_id == 17)


def test_a_physical_hit_on_another_piece_of_the_planned_surface_leaves_the_plan():
    pl = T.planned(SURFS, PLAYER, CURSOR, PLAN, None, HINTS, WEST)
    ph = T.physical(SURFS, PLAYER, CURSOR, PLAN, None, HINTS, WEST)
    a, b = _hit17(pl.events), _hit17(ph.events)
    assert a.applied and b.applied and a.point == b.point   # the same reflection, the same point
    assert not a.on_segment and b.on_segment                # the planned piece is the western one
    assert not a.off_plan and b.off_plan                    # ... the physical walk has left the plan there
    assert not any(e.off_plan for e in pl.events if e.kind != "cursor" and pl.events.index(e) <= pl.events.index(a))


def test_the_cursor_beyond_it_is_not_visible():
    assert V.visible(SURFS, PLAYER, CURSOR, PLAN, None, HINTS, WEST) is False


def test_the_whole_surface_planned_is_aligned():
    # control: an entry without a piece matches the design surface anywhere
    ph = T.physical(SURFS, PLAYER, CURSOR, PLAN, None, HINTS, None)
    assert not _hit17(ph.events).off_plan
    assert V.visible(SURFS, PLAYER, CURSOR, PLAN, None, HINTS, None) is True
