"""Stage 134 (GAME_SPEC 12.1, 14.5, the user): a walk computes the crossings of
its candidate surfaces only -- those whose crossing could matter to it now --
rebuilt where its state changes; visibility compares the walks by their
significant events."""
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
import visibility as V  # noqa: E402
from test_trace2 import PASS, REFL, TERM, emit_infinity, kinds, line  # noqa: E402

mp.mp.dps = 50


def _ids(w):
    return [(e.surface_id, e.applied) for e in w.events if e.kind == "hit"]


def test_a_pure_pass_through_is_never_computed():
    p = line(1, (50, -500), (50, 500), PASS, PASS)
    wall = line(2, (100, -500), (100, 500), TERM, TERM)
    w = T.trace_physical_direction([p, wall], (0, 0), mp.mpc(1, 0))
    assert _ids(w) == [(2, True)]


def test_a_waiting_planned_walk_computes_its_entries_alone():
    # the mirror x = 200 planned; a wall at x = 100 in the way: the planned walk
    # passes it without computing it, the physical walk is blocked there
    wall = line(1, (100, -500), (100, 500), TERM, TERM)
    m = line(2, (200, -500), (200, 500), REFL, REFL)
    with emit_infinity():
        pl = T.planned([wall, m], (0.0, 0.0), (50.0, 30.0), [(2, "RIGHT")])
        ph = T.physical([wall, m], (0.0, 0.0), (50.0, 30.0), [(2, "RIGHT")])
    assert _ids(pl)[0] == (2, True), kinds(pl)
    assert all(sid != 1 for sid, _ in _ids(pl)[: [e.kind for e in pl.events].index("cursor")])
    assert _ids(ph)[0] == (1, True)
    assert V.visible([wall, m], (0.0, 0.0), (50.0, 30.0), [(2, "RIGHT")]) is False


def test_a_ghost_passes_without_computing():
    # ghost mode: a mirror (not a ghost toggler) is passed -- no candidate
    m = line(1, (100, -500), (100, 500), REFL, REFL)
    wall = line(2, (300, -500), (300, 500), TERM, TERM)
    w = T.trace_physical_direction([m, wall], (0, 0), mp.mpc(1, 0), flags={T.GHOST: True})
    assert all(sid != 1 for sid, _ in _ids(w)), kinds(w)


def test_the_candidates_are_rebuilt_where_the_state_changes():
    # a toggler at x = 100 makes a wall at x = 300 present: computed from that hit on
    sets = dict(PASS, interactive=True, states=[{"key": "m", "op": "set", "value": True}])
    toggler = line(1, (100, -500), (100, 500), sets, sets)
    wall = T.surface_from_json({"id": 2, "line": True, "start": [300, -500], "end": [300, 500], "via": [300, 0],
                                "full": False, "left": TERM, "right": TERM, "presence": [{"key": "m", "values": ["true"]}]})
    w = T.trace_physical_direction([toggler, wall], (0, 0), mp.mpc(1, 0))
    assert _ids(w)[:2] == [(1, False), (2, True)], kinds(w)
    assert w.events[1].terminal


def test_visibility_by_significant_events():
    # an off-segment wall crossing (a candidate that fires nothing) on the physical
    # walk alone does not part the walks: the cursor beyond it is visible (Stage 139:
    # it is no event at all any more)
    off = line(1, (100, 100), (100, 200), TERM, TERM)
    m = line(2, (200, -500), (200, 500), REFL, REFL)
    with emit_infinity():
        ph = T.physical([off, m], (0.0, 0.0), (150.0, 30.0), [(2, "RIGHT")])
        pl = T.planned([off, m], (0.0, 0.0), (150.0, 30.0), [(2, "RIGHT")])
    assert not any(e.kind == "hit" and e.surface_id == 1 for e in ph.events)
    assert not any(e.kind == "hit" and e.surface_id == 1 for e in pl.events)
    assert V.visible([off, m], (0.0, 0.0), (150.0, 30.0), [(2, "RIGHT")]) is True
